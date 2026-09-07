// Stage 3 — C++ OpenCV-DNN inference latency benchmark for TinyBeaconNet.
//
// Loads the model ONCE (excluded from timing), warms up, then times a batch
// of detect() calls end-to-end (preprocess + forward + decode). This is NOT
// part of the control path; it only measures whether AI inference itself
// fits inside the closed loop's nominal 20 ms / frame (50 Hz) budget on this
// machine. See docs/19_AI_PERCEPTION_ARCHITECTURE.md and the Stage-3
// checkpoint report §15 for how this number must (and must not) be used.
//
// Usage:
//   ai_inference_benchmark [model_path] [--samples N] [--warmup N]
//
// Default model_path is "models/tiny_beacon_net.onnx" resolved relative to
// the current working directory (run from the project root, matching every
// other fsoc app / script in this repo).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <memory>

#include <opencv2/core.hpp>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/camera.hpp"
#include "fsoc/config.hpp"
#include "fsoc/observation.hpp"
#include "fsoc/renderer.hpp"

using fsoc::AiBeaconDetector;
using fsoc::AiBeaconDetectorConfig;

namespace {

struct Options {
    std::string model_path{"models/tiny_beacon_net.onnx"};
    int samples{500};
    int warmup{50};
};

[[nodiscard]] Options parse_args(const int argc, char** argv) {
    Options opt{};
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--samples" && i + 1 < argc) {
            opt.samples = std::stoi(argv[++i]);
        } else if (arg == "--warmup" && i + 1 < argc) {
            opt.warmup = std::stoi(argv[++i]);
        } else {
            positional.push_back(arg);
        }
    }
    if (!positional.empty()) {
        opt.model_path = positional.front();
    }
    return opt;
}

[[nodiscard]] cv::Mat make_benchmark_frame() {
    // A clean, centered, strongly-lit interior beacon (the same construction
    // used throughout the Step-4/5 test suites): guarantees every call takes
    // the full preprocess+forward+decode path end to end, which is what the
    // 20 ms/frame closed-loop budget cares about. presence_threshold is set
    // to 0.0 by the caller, so classification is irrelevant to this timing.
    const fsoc::CameraConfig camera_config{};
    const fsoc::PanTiltCamera camera{camera_config};
    const fsoc::SyntheticCameraRenderer renderer{fsoc::renderer_config_for(camera_config, 2.0)};
    const fsoc::CameraObservation observation{
        .status = fsoc::ObservationStatus::Visible,
        .image_point_px = fsoc::ImagePoint{.x_px = camera.cx_px(), .y_px = camera.cy_px()},
    };
    return renderer.render(observation);
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);

    AiBeaconDetectorConfig detector_config{};
    detector_config.model_path = opt.model_path;
    detector_config.presence_threshold = 0.0;  // benchmark timing, not acceptance -- run the full path always

    std::unique_ptr<AiBeaconDetector> detector;
    try {
        detector = std::make_unique<AiBeaconDetector>(detector_config);
    } catch (const std::exception& e) {
        std::cerr << "ai_inference_benchmark: failed to load model '" << opt.model_path
                  << "': " << e.what() << "\n"
                  << "Run from the project root, or pass the model path explicitly.\n";
        return 1;
    }

    const cv::Mat frame = make_benchmark_frame();

    std::cout << "AI inference latency benchmark\n"
              << "  model     : " << opt.model_path << "\n"
              << "  warmup    : " << opt.warmup << " calls\n"
              << "  samples   : " << opt.samples << " calls\n";

    for (int i = 0; i < opt.warmup; ++i) {
        (void)detector->detect(frame);
    }

    std::vector<double> latencies_ms;
    latencies_ms.reserve(static_cast<std::size_t>(opt.samples));
    for (int i = 0; i < opt.samples; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto result = detector->detect(frame);
        const auto t1 = std::chrono::steady_clock::now();
        (void)result;
        latencies_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    std::vector<double> sorted = latencies_ms;
    std::sort(sorted.begin(), sorted.end());
    const double mean =
        std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
    const double median = sorted[sorted.size() / 2];
    const auto p95_index =
        static_cast<std::size_t>(std::min<std::size_t>(sorted.size() - 1,
            static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(sorted.size()))) - 1));
    const double p95 = sorted[p95_index];
    const double max_ms = sorted.back();

    std::cout << std::fixed << std::setprecision(4) << "  mean   ms : " << mean << "\n"
              << "  median ms : " << median << "\n"
              << "  p95    ms : " << p95 << "\n"
              << "  max    ms : " << max_ms << "\n\n";

    constexpr double kNominalFrameBudgetMs = 20.0;  // 50 Hz simulation
    if (p95 <= kNominalFrameBudgetMs) {
        std::cout << "AI inference itself fits within the 20 ms nominal frame budget on this "
                     "machine (P95 <= 20 ms). This is NOT a claim of full 50 Hz hardware-loop "
                     "qualification -- only that the model's own compute cost leaves headroom.\n";
    } else {
        std::cout << "AI inference P95 EXCEEDS the 20 ms nominal frame budget on this machine.\n";
    }
    return 0;
}
