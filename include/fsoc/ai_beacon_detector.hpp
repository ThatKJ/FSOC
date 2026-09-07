#pragma once

#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include "fsoc/measurement.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// AI beacon detector (Stage 3) — TinyBeaconNet via OpenCV-DNN / ONNX
// ---------------------------------------------------------------------------
//
// Mirrors the classical BeaconDetector's contract: detect() consumes ONLY a
// CV_8UC1 frame and returns an estimated beacon centroid, or std::nullopt. It
// never sees TargetState, trajectory, CameraObservation, the exact projected
// ImagePoint, TrackingError, or any controller state — see
// docs/19_AI_PERCEPTION_ARCHITECTURE.md and docs/15_INTERFACE_CONTRACTS.md
// ("V2 AI perception contract").
//
// std::nullopt here means "no threshold-accepted candidate this frame" — the
// presence probability was below config().presence_threshold. That gate is
// applied INSIDE detect() (mirrors tools/ai/eval_beacon_net.py's detection
// rule) so a returned AiBeaconDetection always IS a threshold-accepted
// candidate; the caller never re-applies the threshold.
//
// AiBeaconDetection is NOT itself control-authoritative: whether a candidate
// becomes the control-facing detection is decided by the Safe Hybrid policy
// (fsoc/perception.hpp, ADR-018), not by this class.
//
// The numeric contract below (preprocessing, coordinate mapping, heatmap
// decode) is FROZEN and shared bit-for-bit with tools/ai/common.py. Do not
// change a constant here without updating that file, the parity fixture
// (tests/fixtures/ai_parity/), and docs/19 + docs/20 in the same commit.
//
//   CV_8UC1 640x480 --resize(INTER_AREA)--> 320x240 --/255--> NCHW [1,1,240,320]
//       --TinyBeaconNet (ONNX, opset 12)--> presence_logit [1,1] + heatmap_logit [1,1,60,80]
//       --sigmoid + 5x5 soft-argmax--> (x_px, y_px) in ORIGINAL 640x480 pixels

struct AiBeaconDetectorConfig {
    std::string model_path;           // models/tiny_beacon_net.onnx (project-relative)
    double presence_threshold{0.95};  // models/threshold.json (val-calibrated — do not retune here)
    int input_width{320};             // network input width  (native ORIG_W / 2)
    int input_height{240};            // network input height (native ORIG_H / 2)

    // model_path non-empty; presence_threshold finite in [0, 1]; input_width/
    // input_height > 0. Throws std::invalid_argument otherwise. Pure field
    // validation — never touches the filesystem (see AiBeaconDetector ctor).
    void validate() const;
};

struct AiBeaconDetection {
    BeaconDetection detection;  // the controller-facing contract (centroid only)
    double confidence{};        // sigmoid(presence_logit) in [0,1]      — DIAGNOSTIC
    double peak_confidence{};   // heatmap peak sigmoid value            — DIAGNOSTIC
    double inference_ms{};      // preprocess+forward+decode wall time   — DIAGNOSTIC
};

// Raw model outputs, PRE-threshold and PRE-decode. Exists ONLY so the Python
// (onnxruntime) <-> C++ (OpenCV-DNN) numeric parity test can compare the raw
// tensors bit-for-bit against tests/fixtures/ai_parity/expected.json — never
// used by detect() / the Hybrid policy / anything control-facing.
struct AiRawInference {
    double presence_logit{};  // raw logit, sigmoid NOT applied
    cv::Mat heatmap_logit;    // 60x80 CV_32F, raw logits, sigmoid NOT applied
    double inference_ms{};
};

class AiBeaconDetector {
public:
    // Loads and validates the committed ONNX model. Throws:
    //   std::invalid_argument  — invalid config (see AiBeaconDetectorConfig::validate)
    //   std::runtime_error     — missing/unreadable model file, malformed ONNX,
    //                            or an output contract mismatch (wrong output
    //                            count / shape / dtype, non-finite probe output)
    explicit AiBeaconDetector(AiBeaconDetectorConfig config);

    [[nodiscard]] const AiBeaconDetectorConfig& config() const noexcept { return config_; }

    // Pixels only — see class comment. Throws std::invalid_argument for an
    // empty frame or any type other than CV_8UC1 (matches BeaconDetector's
    // validation style). Returns std::nullopt when the presence probability
    // is below config().presence_threshold. Throws std::runtime_error if the
    // model produces a non-finite output — a NaN/Inf centroid never reaches
    // the caller.
    [[nodiscard]] std::optional<AiBeaconDetection> detect(const cv::Mat& frame) const;

    // DIAGNOSTIC / TEST ONLY — see AiRawInference. Same input validation as
    // detect(); runs the identical preprocess+forward path but returns the
    // pre-threshold, pre-decode tensors instead of a gated, decoded result.
    [[nodiscard]] AiRawInference infer_raw(const cv::Mat& frame) const;

private:
    // Throws std::runtime_error if `outputs` does not match the frozen
    // contract: exactly 2 outputs, presence [*,1] / heatmap [*,60,80] shaped
    // (by element count), both CV_32F, both fully finite.
    void validate_outputs(const std::vector<cv::Mat>& outputs) const;

    AiBeaconDetectorConfig config_;

    // cv::dnn::Net::setInput()/forward() are non-const (they mutate internal
    // layer buffers), but from detect()'s caller's perspective evaluating the
    // network is a pure, deterministic, read-only operation — same frame in,
    // same result out. `mutable` here is the minimal way to keep detect()
    // const, matching BeaconDetector::detect()'s const-qualified interface.
    mutable cv::dnn::Net net_;
};

}  // namespace fsoc
