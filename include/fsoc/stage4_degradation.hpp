#pragma once

#include <cstdint>
#include <optional>

#include <opencv2/core.hpp>

namespace fsoc::stage4 {

// ---------------------------------------------------------------------------
// Stage-4 closed-loop degradation post-process (evaluation-only, additive)
// ---------------------------------------------------------------------------
//
// AiFrameSynthesizer (Stage 1, fsoc/ai_frame_synth.hpp) draws its OWN random
// beacon position per sample -- it cannot render a frame at a caller-chosen
// pixel, so it cannot be used to build a frame consistent with a live
// closed-loop trajectory. This module instead applies the SAME conceptual
// degradations (background gradient/vignette, star-like clutter, an optional
// single bright/twin distractor, one optical blur operator, shot + read
// noise, hot/dead/salt pixels), in the same order AiFrameSynthesizer itself
// uses, as a POST-PROCESS on an existing image -- one already rendered at the
// physically-correct truth position by the frozen, unmodified
// SyntheticCameraRenderer (Step 4).
//
// apply_degradation() never reads the true beacon position: clutter/distractor
// points are placed independently of it, exactly like AiFrameSynthesizer's own
// star-clutter stage. It is a pure, seeded function of (frame, seed, config)
// only -- see docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md §3.2. Frozen per that
// protocol; do not retune after seeing Stage-4 results.

enum class OpticalMode { None, GaussianBlur, Defocus, MotionBlur };

struct DegradationConfig {
    // Additive Gaussian read noise (counts) + shot-like noise (variance ~ signal).
    double read_sigma{0.0};
    double shot_scale{0.0};

    int hot_pixel_count{0};
    int dead_pixel_count{0};
    int salt_pixel_count{0};

    double gradient_amplitude{0.0};  // peak-to-trough of a linear background plane
    double vignette_strength{0.0};   // 0 = none, 1 = corners fully dimmed

    OpticalMode optical_mode{OpticalMode::None};
    double optical_param{0.0};       // Gaussian sigma / defocus disk radius / motion length (px)
    double optical_angle_rad{0.0};   // motion blur direction only

    int star_count{0};
    double star_peak_lo{40.0};
    double star_peak_hi{200.0};
    double star_sigma_lo{0.7};
    double star_sigma_hi{1.8};

    bool add_distractor{false};
    double distractor_peak_lo{0.0};
    double distractor_peak_hi{0.0};
    double distractor_sigma_lo{1.4};
    double distractor_sigma_hi{2.2};
};

// `clean_u8` must be CV_8UC1 (the renderer's output, visible-beacon or
// background-only). Returns a new CV_8UC1 frame of the same size; `clean_u8`
// is never modified. Deterministic: equal (clean_u8 bytes, seed, config) ->
// byte-identical output.
[[nodiscard]] cv::Mat apply_degradation(
    const cv::Mat& clean_u8, std::uint64_t seed, const DegradationConfig& config);

}  // namespace fsoc::stage4
