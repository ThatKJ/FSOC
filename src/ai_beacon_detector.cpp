#include "fsoc/ai_beacon_detector.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace fsoc {

namespace {

// ---------------------------------------------------------------------------
// Frozen geometry constants — mirror tools/ai/common.py EXACTLY.
// ---------------------------------------------------------------------------
constexpr int kHmW = 80;               // heatmap head width  (input / 4, native / 8)
constexpr int kHmH = 60;               // heatmap head height (input / 4, native / 8)
constexpr int kInputStride = 8;        // native pixels per heatmap cell (native ORIG_W / kHmW)
constexpr int kDecodeWindowRadius = 2; // soft-argmax window is (2R+1) x (2R+1)

constexpr const char* kOnnxInputName = "input";
constexpr const char* kOnnxPresenceOutput = "presence_logit";
constexpr const char* kOnnxHeatmapOutput = "heatmap_logit";

[[nodiscard]] double sigmoid(const double x) noexcept {
    return 1.0 / (1.0 + std::exp(-x));
}

[[nodiscard]] bool mat_all_finite_f32(const cv::Mat& m) {
    for (int y = 0; y < m.rows; ++y) {
        const auto* row = m.ptr<float>(y);
        for (int x = 0; x < m.cols; ++x) {
            if (!std::isfinite(static_cast<double>(row[x]))) {
                return false;
            }
        }
    }
    return true;
}

struct DecodedPeak {
    double x_orig_px;
    double y_orig_px;
    double peak_confidence;
};

// Mirrors tools/ai/common.py::decode_heatmap exactly: sigmoid -> integer
// argmax (first occurrence in raster order, matching np.argmax) -> 5x5
// soft-argmax (window clamped to the grid) -> map back to original pixels.
[[nodiscard]] DecodedPeak decode_heatmap(const cv::Mat& heatmap_logit_hxw) {
    const int rows = heatmap_logit_hxw.rows;
    const int cols = heatmap_logit_hxw.cols;

    std::vector<double> prob(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
    int iy_best = 0;
    int ix_best = 0;
    double best_prob = -1.0;
    for (int y = 0; y < rows; ++y) {
        const auto* row = heatmap_logit_hxw.ptr<float>(y);
        for (int x = 0; x < cols; ++x) {
            const double p = sigmoid(static_cast<double>(row[x]));
            prob[static_cast<std::size_t>(y) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(x)] = p;
            if (p > best_prob) {  // strict '>' -> first max in raster order, matches np.argmax
                best_prob = p;
                iy_best = y;
                ix_best = x;
            }
        }
    }

    const int r = kDecodeWindowRadius;
    const int y0 = std::max(0, iy_best - r);
    const int y1 = std::min(rows - 1, iy_best + r);
    const int x0 = std::max(0, ix_best - r);
    const int x1 = std::min(cols - 1, ix_best + r);

    double wsum = 0.0;
    double wx = 0.0;
    double wy = 0.0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double w = prob[static_cast<std::size_t>(y) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(x)];
            wsum += w;
            wx += w * static_cast<double>(x);
            wy += w * static_cast<double>(y);
        }
    }

    double xh;
    double yh;
    if (wsum <= 0.0) {
        xh = static_cast<double>(ix_best);
        yh = static_cast<double>(iy_best);
    } else {
        xh = wx / wsum;
        yh = wy / wsum;
    }

    return DecodedPeak{
        .x_orig_px = (xh + 0.5) * static_cast<double>(kInputStride) - 0.5,
        .y_orig_px = (yh + 0.5) * static_cast<double>(kInputStride) - 0.5,
        .peak_confidence = best_prob,
    };
}

}  // namespace

void AiBeaconDetectorConfig::validate() const {
    if (model_path.empty()) {
        throw std::invalid_argument("AiBeaconDetectorConfig: model_path must not be empty.");
    }
    if (!std::isfinite(presence_threshold) || presence_threshold < 0.0 || presence_threshold > 1.0) {
        throw std::invalid_argument("AiBeaconDetectorConfig: presence_threshold must be finite in [0, 1].");
    }
    if (input_width <= 0 || input_height <= 0) {
        throw std::invalid_argument("AiBeaconDetectorConfig: input_width/input_height must be > 0.");
    }
}

AiBeaconDetector::AiBeaconDetector(AiBeaconDetectorConfig config) : config_(std::move(config)) {
    config_.validate();

    {
        std::ifstream probe(config_.model_path, std::ios::binary);
        if (!probe.good()) {
            throw std::runtime_error(
                "AiBeaconDetector: cannot read model file: " + config_.model_path);
        }
    }

    try {
        net_ = cv::dnn::readNetFromONNX(config_.model_path);
    } catch (const cv::Exception& e) {
        throw std::runtime_error(
            "AiBeaconDetector: failed to load ONNX model '" + config_.model_path + "': " + e.what());
    }
    if (net_.empty()) {
        throw std::runtime_error(
            "AiBeaconDetector: ONNX model loaded but produced an empty network: " + config_.model_path);
    }
    // Backend/target selection intentionally left at OpenCV's default: this
    // OpenCV 5 build's graph engine already runs ONNX opset-12 graphs on CPU
    // without an explicit DNN_BACKEND_OPENCV/DNN_TARGET_CPU request (which
    // this build warns is ignored by the new engine, with no effect either
    // way) -- deterministic CPU execution either way (see determinism test).

    // Probe forward pass on a zero input: validates the output contract
    // (count / shape / dtype / finiteness) once at construction time, so
    // detect() always runs against a known-good graph.
    const int probe_shape[4] = {1, 1, config_.input_height, config_.input_width};
    const cv::Mat probe_blob = cv::Mat::zeros(4, probe_shape, CV_32F);
    net_.setInput(probe_blob, kOnnxInputName);
    std::vector<cv::Mat> outputs;
    try {
        net_.forward(outputs, std::vector<std::string>{kOnnxPresenceOutput, kOnnxHeatmapOutput});
    } catch (const cv::Exception& e) {
        throw std::runtime_error(
            "AiBeaconDetector: probe forward pass failed (unexpected ONNX graph/output names): " +
            std::string(e.what()));
    }
    validate_outputs(outputs);
}

void AiBeaconDetector::validate_outputs(const std::vector<cv::Mat>& outputs) const {
    if (outputs.size() != 2) {
        throw std::runtime_error(
            "AiBeaconDetector: expected exactly 2 model outputs (presence_logit, heatmap_logit), got " +
            std::to_string(outputs.size()) + ".");
    }
    const cv::Mat& presence = outputs[0];
    const cv::Mat& heatmap = outputs[1];

    if (presence.type() != CV_32F || heatmap.type() != CV_32F) {
        throw std::runtime_error("AiBeaconDetector: model outputs must be CV_32F.");
    }
    if (presence.total() != 1) {
        throw std::runtime_error(
            "AiBeaconDetector: presence_logit output must have exactly 1 element, got " +
            std::to_string(presence.total()) + ".");
    }
    const auto expected_hm_elems = static_cast<std::size_t>(kHmH) * static_cast<std::size_t>(kHmW);
    if (heatmap.total() != expected_hm_elems) {
        throw std::runtime_error(
            "AiBeaconDetector: heatmap_logit output must have " + std::to_string(expected_hm_elems) +
            " elements (" + std::to_string(kHmH) + "x" + std::to_string(kHmW) + "), got " +
            std::to_string(heatmap.total()) + ".");
    }
    if (!mat_all_finite_f32(presence.reshape(1, 1))) {
        throw std::runtime_error("AiBeaconDetector: presence_logit output is non-finite (NaN/Inf).");
    }
    if (!mat_all_finite_f32(heatmap.reshape(1, kHmH))) {
        throw std::runtime_error("AiBeaconDetector: heatmap_logit output is non-finite (NaN/Inf).");
    }
}

namespace {

void validate_frame(const cv::Mat& frame, const char* caller) {
    if (frame.empty()) {
        throw std::invalid_argument(std::string(caller) + ": frame is empty.");
    }
    if (frame.type() != CV_8UC1) {
        throw std::invalid_argument(
            std::string(caller) +
            ": frame must be CV_8UC1 (single-channel 8-bit); RGB / floating-point frames "
            "are not accepted.");
    }
}

}  // namespace

AiRawInference AiBeaconDetector::infer_raw(const cv::Mat& frame) const {
    validate_frame(frame, "AiBeaconDetector::infer_raw");

    const auto t0 = std::chrono::steady_clock::now();

    // Frozen preprocessing: cv2.resize(..., INTER_AREA) -> float32 /255 -> NCHW.
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(config_.input_width, config_.input_height), 0, 0, cv::INTER_AREA);
    const cv::Mat blob =
        cv::dnn::blobFromImage(resized, 1.0 / 255.0, cv::Size(), cv::Scalar(), false, false, CV_32F);

    net_.setInput(blob, kOnnxInputName);
    std::vector<cv::Mat> outputs;
    net_.forward(outputs, std::vector<std::string>{kOnnxPresenceOutput, kOnnxHeatmapOutput});
    validate_outputs(outputs);

    const auto t1 = std::chrono::steady_clock::now();

    AiRawInference result{};
    result.presence_logit = static_cast<double>(outputs[0].reshape(1, 1).at<float>(0, 0));
    result.heatmap_logit = outputs[1].reshape(1, kHmH).clone();  // detach from net's internal buffer
    result.inference_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

std::optional<AiBeaconDetection> AiBeaconDetector::detect(const cv::Mat& frame) const {
    const AiRawInference raw = infer_raw(frame);

    const double presence_probability = sigmoid(raw.presence_logit);
    const DecodedPeak peak = decode_heatmap(raw.heatmap_logit);

    if (!std::isfinite(presence_probability) || !std::isfinite(peak.x_orig_px) ||
        !std::isfinite(peak.y_orig_px) || !std::isfinite(peak.peak_confidence)) {
        throw std::runtime_error("AiBeaconDetector::detect: decoded output is non-finite.");
    }

    if (presence_probability < config_.presence_threshold) {
        return std::nullopt;  // no threshold-accepted candidate this frame
    }

    return AiBeaconDetection{
        .detection = BeaconDetection{.centroid_px = {.x_px = peak.x_orig_px, .y_px = peak.y_orig_px}},
        .confidence = presence_probability,
        .peak_confidence = peak.peak_confidence,
        .inference_ms = raw.inference_ms,
    };
}

}  // namespace fsoc
