#include "fsoc/demo_disturbance.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace fsoc {

const char* to_string(const DemoDisturbanceKind kind) noexcept {
    switch (kind) {
        case DemoDisturbanceKind::None: return "NONE";
        case DemoDisturbanceKind::Noise: return "NOISE";
        case DemoDisturbanceKind::Clutter: return "CLUTTER";
        case DemoDisturbanceKind::Occlusion: return "OCCLUSION";
    }
    return "NONE";
}

void DemoDisturbanceConfig::validate() const {
    if (!(std::isfinite(noise_sigma) && noise_sigma >= 0.0)) {
        throw std::invalid_argument("DemoDisturbanceConfig: noise_sigma must be finite and >= 0.");
    }
    if (!(std::isfinite(distractor_peak_lo) && std::isfinite(distractor_peak_hi) && distractor_peak_lo >= 0.0 &&
          distractor_peak_hi <= 255.0 && distractor_peak_lo <= distractor_peak_hi)) {
        throw std::invalid_argument(
            "DemoDisturbanceConfig: distractor_peak_lo/hi must be finite, in [0, 255], with lo <= hi.");
    }
    if (!(std::isfinite(distractor_sigma_lo) && std::isfinite(distractor_sigma_hi) && distractor_sigma_lo > 0.0 &&
          distractor_sigma_lo <= distractor_sigma_hi)) {
        throw std::invalid_argument(
            "DemoDisturbanceConfig: distractor_sigma_lo/hi must be finite, > 0, with lo <= hi.");
    }
}

namespace {

void add_gaussian_blob(cv::Mat& frame_u8, const double cx, const double cy, const double peak, const double sigma) {
    const double window = 4.0 * sigma;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - window)));
    const int x1 = std::min(frame_u8.cols - 1, static_cast<int>(std::ceil(cx + window)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - window)));
    const int y1 = std::min(frame_u8.rows - 1, static_cast<int>(std::ceil(cy + window)));
    const double inv_two_sigma_sq = 1.0 / (2.0 * sigma * sigma);
    for (int y = y0; y <= y1; ++y) {
        auto* row = frame_u8.ptr<std::uint8_t>(y);
        const double dy = static_cast<double>(y) - cy;
        for (int x = x0; x <= x1; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double g = peak * std::exp(-(dx * dx + dy * dy) * inv_two_sigma_sq);
            const double value = static_cast<double>(row[x]) + g;
            row[x] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
        }
    }
}

}  // namespace

cv::Mat apply_demo_disturbance(
    const cv::Mat& clean_u8, const std::size_t frame_index, const DemoDisturbanceConfig& config) {
    if (clean_u8.empty() || clean_u8.type() != CV_8UC1) {
        throw std::invalid_argument("apply_demo_disturbance: clean_u8 must be a non-empty CV_8UC1 frame.");
    }
    config.validate();

    cv::Mat out = clean_u8.clone();
    if (config.kind == DemoDisturbanceKind::None) {
        return out;
    }

    if (config.kind == DemoDisturbanceKind::Occlusion) {
        const std::size_t window_end = config.occlusion_start_frame + config.occlusion_duration_frames;
        if (frame_index >= config.occlusion_start_frame && frame_index < window_end) {
            out.setTo(cv::Scalar(static_cast<double>(config.occlusion_background_intensity)));
        }
        return out;
    }

    // Noise/Clutter: deterministic seed mixed from frame_index only (splitmix64-style).
    std::uint64_t seed = static_cast<std::uint64_t>(frame_index) + 0x9E3779B97F4A7C15ULL;
    seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBULL;
    seed ^= (seed >> 31);
    std::mt19937_64 rng{seed};

    if (config.kind == DemoDisturbanceKind::Noise) {
        std::normal_distribution<double> noise{0.0, config.noise_sigma};
        for (int y = 0; y < out.rows; ++y) {
            auto* row = out.ptr<std::uint8_t>(y);
            for (int x = 0; x < out.cols; ++x) {
                const double value = static_cast<double>(row[x]) + noise(rng);
                row[x] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
            }
        }
        return out;
    }

    // Clutter: one bright distractor at a uniformly random position, wholly
    // independent of the beacon (this function never sees the beacon's
    // position -- it only sees the already-rendered pixels).
    std::uniform_real_distribution<double> ux{0.0, static_cast<double>(out.cols - 1)};
    std::uniform_real_distribution<double> uy{0.0, static_cast<double>(out.rows - 1)};
    std::uniform_real_distribution<double> upeak{config.distractor_peak_lo, config.distractor_peak_hi};
    std::uniform_real_distribution<double> usigma{config.distractor_sigma_lo, config.distractor_sigma_hi};
    add_gaussian_blob(out, ux(rng), uy(rng), upeak(rng), usigma(rng));
    return out;
}

}  // namespace fsoc
