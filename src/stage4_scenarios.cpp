#include "fsoc/stage4_scenarios.hpp"

#include <numbers>
#include <stdexcept>

namespace fsoc::stage4 {

namespace {
constexpr double kPi = std::numbers::pi_v<double>;
}  // namespace

std::string_view scenario_token(const ScenarioId id) {
    switch (id) {
        case ScenarioId::Clean: return "A";
        case ScenarioId::LowSnr: return "B";
        case ScenarioId::StarClutter: return "C";
        case ScenarioId::HotPixels: return "D";
        case ScenarioId::BrightDistractor: return "E";
        case ScenarioId::Blur: return "F";
        case ScenarioId::MotionBlur: return "G";
        case ScenarioId::BackgroundGradient: return "H";
        case ScenarioId::TargetAbsent: return "I";
        case ScenarioId::MixedRandomized: return "J";
        case ScenarioId::AdversarialAmbiguity: return "K";
    }
    throw std::invalid_argument("stage4::scenario_token: unknown ScenarioId");
}

std::string_view scenario_name(const ScenarioId id) {
    switch (id) {
        case ScenarioId::Clean: return "CLEAN";
        case ScenarioId::LowSnr: return "LOW_SNR";
        case ScenarioId::StarClutter: return "STAR_CLUTTER";
        case ScenarioId::HotPixels: return "HOT_PIXELS";
        case ScenarioId::BrightDistractor: return "BRIGHT_DISTRACTOR";
        case ScenarioId::Blur: return "BLUR_DEFOCUS";
        case ScenarioId::MotionBlur: return "MOTION_BLUR";
        case ScenarioId::BackgroundGradient: return "BACKGROUND_GRADIENT";
        case ScenarioId::TargetAbsent: return "TARGET_ABSENT";
        case ScenarioId::MixedRandomized: return "MIXED_RANDOMIZED";
        case ScenarioId::AdversarialAmbiguity: return "ADVERSARIAL_IDENTITY_AMBIGUITY";
    }
    throw std::invalid_argument("stage4::scenario_name: unknown ScenarioId");
}

int scenario_index(const ScenarioId id) {
    for (std::size_t i = 0; i < kAllScenarios.size(); ++i) {
        if (kAllScenarios[i] == id) {
            return static_cast<int>(i);
        }
    }
    throw std::invalid_argument("stage4::scenario_index: unknown ScenarioId");
}

std::uint64_t scenario_stream_seed(const int scenario_idx, const std::uint64_t base_seed) {
    constexpr std::uint64_t kMixConstant = 0x9E3779B97F4A7C15ULL;
    return fsoc::ai::splitmix64(base_seed ^ (kMixConstant * static_cast<std::uint64_t>(scenario_idx + 1)));
}

std::uint64_t frame_seed(const int scenario_idx, const std::uint64_t base_seed, const std::uint64_t frame_index) {
    return fsoc::ai::sample_seed_for(scenario_stream_seed(scenario_idx, base_seed), frame_index);
}

namespace {

[[nodiscard]] fsoc::ai::Range fixed(const double v) { return fsoc::ai::Range{v, v}; }
[[nodiscard]] fsoc::ai::Range range(const double lo, const double hi) { return fsoc::ai::Range{lo, hi}; }

// Common-frame baseline floor (protocol §5).
[[nodiscard]] fsoc::ai::AiFrameSynthConfig floor_config() {
    fsoc::ai::AiFrameSynthConfig c{};
    c.negative_fraction = 0.2;

    c.beacon.peak_intensity = range(150.0, 255.0);
    c.beacon.sigma_px = range(1.4, 2.2);
    c.beacon.anisotropy_ratio = fixed(1.0);
    c.beacon.elongation_probability = 0.0;
    c.beacon.max_edge_overshoot_px = 6.0;

    c.background.dark_offset = range(2.0, 6.0);
    c.background.gradient_amplitude = fixed(0.0);
    c.background.vignette_strength = fixed(0.0);

    c.noise.read_sigma = range(1.0, 2.0);
    c.noise.shot_scale = range(0.0, 0.1);
    c.noise.hot_pixel_count = fixed(0.0);
    c.noise.dead_pixel_count = fixed(0.0);
    c.noise.salt_pixel_count = fixed(0.0);

    c.optical.weight_none = 1.0;
    c.optical.weight_gaussian_blur = 0.0;
    c.optical.weight_defocus = 0.0;
    c.optical.weight_motion_blur = 0.0;

    c.clutter.star_count = fixed(0.0);
    c.clutter.bright_distractor_probability = 0.0;
    c.clutter.cluster_probability = 0.0;

    return c;
}

}  // namespace

fsoc::ai::AiFrameSynthConfig common_frame_config(const ScenarioId id) {
    if (id == ScenarioId::MixedRandomized) {
        return fsoc::ai::AiFrameSynthConfig{};  // Stage-1's own default training distribution, verbatim
    }

    fsoc::ai::AiFrameSynthConfig c = floor_config();
    switch (id) {
        case ScenarioId::Clean:
            break;
        case ScenarioId::LowSnr:
            c.beacon.peak_intensity = range(40.0, 90.0);
            c.noise.read_sigma = range(6.0, 10.0);
            c.noise.shot_scale = range(0.6, 1.0);
            break;
        case ScenarioId::StarClutter:
            c.clutter.star_count = range(6.0, 9.0);
            c.clutter.star_peak_intensity = range(60.0, 150.0);
            c.clutter.cluster_probability = 0.5;
            break;
        case ScenarioId::HotPixels:
            c.noise.hot_pixel_count = range(20.0, 40.0);
            break;
        case ScenarioId::BrightDistractor:
            c.clutter.bright_distractor_probability = 1.0;
            c.clutter.bright_distractor_excess = range(20.0, 45.0);
            break;
        case ScenarioId::Blur:
            c.optical.weight_none = 0.0;
            c.optical.weight_gaussian_blur = 0.5;
            c.optical.weight_defocus = 0.5;
            c.optical.gaussian_blur_sigma_px = range(1.5, 2.2);
            c.optical.defocus_radius_px = range(2.0, 3.5);
            break;
        case ScenarioId::MotionBlur:
            c.optical.weight_none = 0.0;
            c.optical.weight_motion_blur = 1.0;
            c.optical.motion_blur_length_px = range(6.0, 11.0);
            c.optical.motion_blur_angle_rad = range(0.0, kPi);
            break;
        case ScenarioId::BackgroundGradient:
            c.background.gradient_amplitude = range(18.0, 26.0);
            c.background.vignette_strength = range(0.2, 0.35);
            break;
        case ScenarioId::TargetAbsent:
            c.negative_fraction = 1.0;
            c.clutter.star_count = range(3.0, 7.0);
            c.clutter.star_peak_intensity = range(60.0, 180.0);
            break;
        case ScenarioId::MixedRandomized:
            break;  // handled above
        case ScenarioId::AdversarialAmbiguity:
            c.clutter.bright_distractor_probability = 1.0;
            c.clutter.bright_distractor_excess = range(0.0, 3.0);
            c.clutter.star_sigma_px = range(1.4, 2.2);
            break;
    }
    return c;
}

fsoc::RendererConfig closed_loop_renderer_config(const ScenarioId id) {
    fsoc::RendererConfig cfg{};  // floor: peak 255 / background 5 / sigma 2.0
    switch (id) {
        case ScenarioId::LowSnr:
            cfg.beacon_peak_intensity = 90;
            cfg.background_intensity = 20;
            break;
        case ScenarioId::BrightDistractor:
        case ScenarioId::AdversarialAmbiguity:
            cfg.beacon_peak_intensity = 200;
            break;
        default:
            break;
    }
    return cfg;
}

DegradationConfig closed_loop_degradation_config(const ScenarioId id, const std::size_t seed_index) {
    DegradationConfig c{};  // floor: read_sigma 1.5, shot_scale 0.05, everything else off
    c.read_sigma = 1.5;
    c.shot_scale = 0.05;

    switch (id) {
        case ScenarioId::Clean:
            break;
        case ScenarioId::LowSnr:
            c.read_sigma = 8.0;
            c.shot_scale = 0.8;
            break;
        case ScenarioId::StarClutter:
            c.star_count = 8;
            c.star_peak_lo = 60.0;
            c.star_peak_hi = 150.0;
            c.star_sigma_lo = 0.7;
            c.star_sigma_hi = 1.8;
            break;
        case ScenarioId::HotPixels:
            c.hot_pixel_count = 30;
            break;
        case ScenarioId::BrightDistractor:
            c.add_distractor = true;
            c.distractor_peak_lo = 230.0;
            c.distractor_peak_hi = 255.0;
            c.distractor_sigma_lo = 1.4;
            c.distractor_sigma_hi = 2.2;
            break;
        case ScenarioId::Blur:
            if (seed_index % 2 == 0) {
                c.optical_mode = OpticalMode::GaussianBlur;
                c.optical_param = 2.0;
            } else {
                c.optical_mode = OpticalMode::Defocus;
                c.optical_param = 3.0;
            }
            break;
        case ScenarioId::MotionBlur:
            c.optical_mode = OpticalMode::MotionBlur;
            c.optical_param = 9.0;
            c.optical_angle_rad = 0.6;
            break;
        case ScenarioId::BackgroundGradient:
            c.gradient_amplitude = 24.0;
            c.vignette_strength = 0.3;
            break;
        case ScenarioId::TargetAbsent:
            c.star_count = 4;
            c.star_peak_lo = 60.0;
            c.star_peak_hi = 180.0;
            break;
        case ScenarioId::MixedRandomized:
            c.read_sigma = 4.0;
            c.shot_scale = 0.3;
            c.hot_pixel_count = 8;
            c.gradient_amplitude = 12.0;
            c.vignette_strength = 0.15;
            c.star_count = 3;
            c.optical_mode = OpticalMode::GaussianBlur;
            c.optical_param = 1.0;
            break;
        case ScenarioId::AdversarialAmbiguity:
            c.add_distractor = true;
            c.distractor_peak_lo = 195.0;
            c.distractor_peak_hi = 205.0;
            c.distractor_sigma_lo = 1.8;
            c.distractor_sigma_hi = 2.2;
            break;
    }
    return c;
}

fsoc::Vec3 closed_loop_target_position_m(const ScenarioId id) {
    if (id == ScenarioId::TargetAbsent) {
        return fsoc::Vec3{100.0, 500.0, 0.0};  // guaranteed OutsideFieldOfView
    }
    return fsoc::Vec3{100.0, 10.0, 3.0};  // off-center, inside FOV, non-trivial
}

}  // namespace fsoc::stage4
