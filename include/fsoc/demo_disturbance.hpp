#pragma once

#include <cstdint>

#include <opencv2/core.hpp>

namespace fsoc {

// ---------------------------------------------------------------------------
// Demo disturbance  (MVP-V2 Phase J — additive, presentation-facing)
// ---------------------------------------------------------------------------
//
// A minimal, deterministic image-space disturbance applied AFTER
// SyntheticCameraRenderer (Step 4, frozen) and BEFORE detection, for the
// live/interactive demo's named disturbance scenarios (docs/MVP_ABLATION.md,
// DECISIONS.md ADR-020). This is NOT the Stage-4 evaluation degradation
// module (fsoc::stage4::apply_degradation, docs/21) -- that module is
// evaluation-only and frozen to its own 11-scenario protocol; this is a
// separate, much smaller primitive purpose-built for a handful of
// judge-facing demo presets. Reuses no Stage-4 code, and Stage-4 is
// untouched by this file.
//
// Deterministic: identical (clean_u8, seed, config) -> byte-identical output.
// Never reads TargetState, trajectory truth, or the exact Projection -- pure
// function of an already-rendered image (the ADR-004 ground-truth boundary
// applies here exactly as it does to every detector/degradation stage).

enum class DemoDisturbanceKind {
    None,       // pass the clean frame through unchanged
    Noise,      // additive Gaussian read noise
    Clutter,    // one bright distractor blob, uniformly random position, reseeded every frame
    Occlusion,  // beacon erased (frame replaced by background) for a scheduled frame window
};

[[nodiscard]] const char* to_string(DemoDisturbanceKind kind) noexcept;

struct DemoDisturbanceConfig {
    DemoDisturbanceKind kind{DemoDisturbanceKind::None};

    // Noise: additive N(0, noise_sigma) per pixel, clamped to [0,255].
    double noise_sigma{0.0};

    // Clutter: one Gaussian blob at a uniformly random (x,y), independent of
    // the beacon and re-rolled every frame -- the same conceptual shape as
    // Stage-4's BrightDistractor (docs/MVP_ABLATION.md §3), deliberately
    // brighter/larger than the default beacon (peak=255, sigma=2.0px,
    // RendererConfig) so Classical's brightest-connected-component rule
    // genuinely has to choose between them.
    double distractor_peak{255.0};
    double distractor_sigma_px{3.0};

    // Occlusion: within the half-open frame-index window
    // [occlusion_start_frame, occlusion_start_frame + occlusion_duration_frames),
    // the frame is replaced with a flat background-intensity image, erasing
    // the beacon -- a deterministic, image-space proxy for a physical
    // obstruction (this simulator has no separate occluding-object
    // renderer; see apps/mvp_dynamic_scenarios.cpp's BriefDropoutTrajectory
    // for the alternative TRAJECTORY-space proxy Phase G used). Not
    // randomized -- the same seed/frame_index always produces the same
    // window.
    std::size_t occlusion_start_frame{0};
    std::size_t occlusion_duration_frames{0};
    std::uint8_t occlusion_background_intensity{5};  // matches RendererConfig's own default

    // noise_sigma >= 0; distractor_peak in [0,255]; distractor_sigma_px finite > 0.
    // Throws std::invalid_argument otherwise. occlusion_* fields have no
    // invalid range (any std::size_t / std::uint8_t value is acceptable; a
    // zero-length window is simply never active).
    void validate() const;
};

// `clean_u8` must be CV_8UC1 (SyntheticCameraRenderer's own output format).
// `frame_index` is the ONLY source of both the Occlusion window position and
// the deterministic seed used for Noise/Clutter's own randomness (mixed
// internally) -- the same frame_index always produces byte-identical output.
// kind == None returns a copy of clean_u8 unchanged. Throws
// std::invalid_argument if clean_u8 is not CV_8UC1.
[[nodiscard]] cv::Mat apply_demo_disturbance(
    const cv::Mat& clean_u8, std::size_t frame_index, const DemoDisturbanceConfig& config);

}  // namespace fsoc
