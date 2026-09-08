#pragma once

#include <opencv2/core.hpp>

namespace fsoc {

// ---------------------------------------------------------------------------
// Real-camera preprocessing (Phase 5) — real frames -> the detector contract
// ---------------------------------------------------------------------------
//
// BeaconDetector::detect() requires CV_8UC1. AiBeaconDetector::detect() has a
// stronger, FROZEN requirement: its heatmap-to-pixel decode uses a fixed
// kInputStride (native pixels per heatmap cell), which is only correct when
// the frame handed to it is exactly the network's native resolution
// (640x480 by default — see fsoc/ai_beacon_detector.hpp). A real phone-camera
// frame at any other resolution MUST be resized to that exact size before
// either detector runs, or classical/AI pixel coordinates (and therefore the
// Hybrid agreement radius) become meaningless.
//
// This function does ONLY that conversion — grayscale + resize — and nothing
// speculative (no denoise/exposure/ROI by default): Phase 5 explicitly says
// not to hardcode preprocessing until it has been checked against real
// frames. Additional operations belong here later, behind their own config
// flags, once real-frame evidence justifies them.

struct LivePreprocessConfig {
    int target_width_px{640};
    int target_height_px{480};
};

// Converts an arbitrary raw camera frame (any channel count, any resolution)
// into a CV_8UC1 frame at exactly (target_width_px, target_height_px).
// Grayscale conversion (cv::cvtColor) is applied only if `raw` has more than
// one channel. Resize uses INTER_AREA, matching the frozen AI preprocessing
// convention (fsoc/ai_beacon_detector.hpp, tools/ai/common.py). Throws
// std::invalid_argument if `raw` is empty or target dimensions are <= 0.
[[nodiscard]] cv::Mat preprocess_live_frame(const cv::Mat& raw, const LivePreprocessConfig& config = {});

}  // namespace fsoc
