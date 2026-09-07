#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace fsoc::ai {

// ===========================================================================
// AI dataset frame synthesizer  (V2 AI PERCEPTION — additive, post-v1_baseline)
// ===========================================================================
//
// Produces domain-randomized single-channel optical frames for TRAINING and
// EVALUATION of the learned beacon detector (TinyBeaconNet). This is dataset
// tooling — it is NOT in the closed-loop control path and does NOT replace
// `SyntheticCameraRenderer`:
//
//   * `SyntheticCameraRenderer` (Step 4, FROZEN) renders the clean analytic
//     Gaussian beacon the validated classical baseline is tuned against. It is
//     byte-for-byte unchanged and stays the source of truth for the closed loop.
//   * `AiFrameSynthesizer` (this file) starts from the SAME analytic Gaussian
//     beacon model and then layers the difficult optical conditions the AI must
//     be robust to: low SNR, background gradient, vignette, sensor noise, hot /
//     dead / salt pixels, blur, defocus, motion blur, star-like clutter, bright
//     distractors, partial edge clipping, and target-absent frames.
//
// The synthesizer is a PURE, SEEDED function of its per-sample seed: the same
// seed always yields a byte-identical `cv::Mat` and identical labels, on any
// platform. It draws all randomness from `std::mt19937_64` (never `cv::randn` /
// `cv::randu`, whose stream is not portably reproducible).
//
// Coordinate convention (FROZEN, identical to the rest of the repo):
//   origin = top-left, +x_px = right, +y_px = down. Labels `x_px` / `y_px` are
//   the beacon's true sub-pixel centre in ORIGINAL frame pixels.
//
// -------------------------------------------------------------------------
// Frozen operation order (a sample is built in exactly these stages):
//   1. float working buffer, filled with `background.dark_offset`
//   2. + linear background gradient plane, * radial vignette
//   3. positive sample: + analytic Gaussian beacon at sub-pixel (x,y), peak,
//      sigma, optional mild anisotropy + rotation; may sit partly off-image
//   4. + K star-like clutter spots; optionally one forced brighter than the
//      beacon; optional tight cluster
//   5. one optical-degradation operator, chosen by weight:
//      none | Gaussian blur | defocus disk | linear motion blur
//   6. Poisson shot-like noise (variance ∝ signal), then Gaussian read noise
//   7. hot pixels (→ near max), dead pixels (→ 0), salt impulses (→ max)
//   8. clamp [0,255], round to CV_8UC1
// Negatives skip stage 3 only; every other stage still runs.
// -------------------------------------------------------------------------

inline constexpr const char* kFrameSynthVersion = "1.0.0";

// Inclusive numeric range, drawn uniformly. `lo <= hi` and both finite.
struct Range {
    double lo{0.0};
    double hi{0.0};

    [[nodiscard]] bool degenerate() const noexcept { return lo == hi; }
    void validate(const char* field) const;
};

struct BeaconSynthConfig {
    Range peak_intensity{70.0, 255.0};    // 8-bit counts at the beacon centre
    Range sigma_px{1.2, 3.2};             // isotropic Gaussian sigma, pixels
    Range anisotropy_ratio{1.0, 1.6};     // major/minor sigma ratio (1 = round)
    Range rotation_rad{0.0, 3.14159265358979};
    double elongation_probability{0.25};  // P(apply anisotropy at all)
    // A positive sample's centre may fall this many pixels outside the image
    // (partial edge clipping). Beyond it the frame is treated as a negative.
    double max_edge_overshoot_px{6.0};

    void validate() const;
};

struct BackgroundSynthConfig {
    Range dark_offset{2.0, 14.0};          // uniform pedestal, counts
    Range gradient_amplitude{0.0, 26.0};   // peak-to-trough of the linear plane
    Range vignette_strength{0.0, 0.35};    // 0 = none, 0.35 = corners 35% dimmer
    void validate() const;
};

struct NoiseSynthConfig {
    Range read_sigma{1.0, 6.0};            // additive Gaussian, counts
    Range shot_scale{0.0, 1.0};            // 0 = off, 1 = full Poisson variance
    Range hot_pixel_count{0.0, 12.0};      // isolated near-max pixels
    Range dead_pixel_count{0.0, 6.0};      // isolated zeroed pixels
    Range salt_pixel_count{0.0, 20.0};     // impulse (max) pixels
    void validate() const;
};

struct OpticalSynthConfig {
    double weight_none{0.45};
    double weight_gaussian_blur{0.25};
    double weight_defocus{0.15};
    double weight_motion_blur{0.15};
    Range gaussian_blur_sigma_px{0.6, 2.2};
    Range defocus_radius_px{1.0, 3.5};
    Range motion_blur_length_px{3.0, 11.0};
    Range motion_blur_angle_rad{0.0, 3.14159265358979};
    void validate() const;
};

struct ClutterSynthConfig {
    Range star_count{0.0, 9.0};            // star-like point distractors
    Range star_peak_intensity{40.0, 200.0};
    Range star_sigma_px{0.7, 1.8};
    double bright_distractor_probability{0.30};  // force one star > beacon peak
    Range bright_distractor_excess{6.0, 45.0};   // counts above the beacon peak
    double cluster_probability{0.20};             // add a tight 2-4 spot cluster
    void validate() const;
};

struct AiFrameSynthConfig {
    int width_px{640};
    int height_px{480};

    // Fraction of generated samples that contain NO beacon. The AI must learn to
    // return "no target" (→ std::nullopt through the perception contract).
    double negative_fraction{0.25};

    BeaconSynthConfig beacon{};
    BackgroundSynthConfig background{};
    NoiseSynthConfig noise{};
    OpticalSynthConfig optical{};
    ClutterSynthConfig clutter{};

    // width/height > 0, negative_fraction in [0,1], every sub-config valid.
    void validate() const;
};

// The concrete randomized parameters chosen for one sample. Recorded in the
// dataset manifest so any frame can be explained / regenerated / stratified.
struct DegradationSample {
    bool target_present{false};
    double beacon_peak{0.0};
    double beacon_sigma_px{0.0};
    double beacon_anisotropy{1.0};
    double beacon_rotation_rad{0.0};
    bool beacon_edge_clipped{false};

    double dark_offset{0.0};
    double gradient_amplitude{0.0};
    double vignette_strength{0.0};

    std::string optical_mode{"none"};   // none | gaussian_blur | defocus | motion_blur
    double optical_param{0.0};          // sigma / radius / length (mode dependent)
    double optical_angle_rad{0.0};      // motion blur only

    double read_sigma{0.0};
    double shot_scale{0.0};
    int hot_pixels{0};
    int dead_pixels{0};
    int salt_pixels{0};

    int star_count{0};
    bool has_bright_distractor{false};
    double brightest_distractor_peak{0.0};
    bool has_cluster{false};

    // Rough single-number difficulty proxy in [0,1] (documented in
    // docs/20_AI_DATASET_AND_TRAINING.md). For stratified reporting only — never
    // a label and never seen by the model.
    double difficulty{0.0};
};

struct SynthFrame {
    cv::Mat image;               // CV_8UC1, config size
    bool target_present{false};
    double x_px{0.0};            // beacon centre, original pixels (valid iff target_present)
    double y_px{0.0};
    DegradationSample params{};
};

// splitmix64 — a tiny, portable, well-distributed integer mixer. Used to derive
// per-sample seeds from (dataset_seed, index) so the sample stream is stable and
// index-addressable regardless of generation order or split layout.
[[nodiscard]] std::uint64_t splitmix64(std::uint64_t x) noexcept;

// Deterministic per-sample seed for global sample `index` under `dataset_seed`.
[[nodiscard]] std::uint64_t sample_seed_for(std::uint64_t dataset_seed, std::uint64_t index) noexcept;

class AiFrameSynthesizer {
public:
    explicit AiFrameSynthesizer(AiFrameSynthConfig config);

    [[nodiscard]] const AiFrameSynthConfig& config() const noexcept { return config_; }

    // Build one sample from `sample_seed`. Pure and deterministic: equal seeds →
    // byte-identical `image` and identical labels. If `force_target` is set it
    // overrides the config's negative_fraction draw for this sample (used by the
    // dataset splitter to hit an exact positive/negative ratio and by tests).
    [[nodiscard]] SynthFrame synthesize(
        std::uint64_t sample_seed,
        std::optional<bool> force_target = std::nullopt) const;

private:
    AiFrameSynthConfig config_;
};

}  // namespace fsoc::ai
