#include "fsoc/stage4_degradation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace fsoc::stage4 {

namespace {

using Rng = std::mt19937_64;

[[nodiscard]] double uniform(Rng& rng, const double lo, const double hi) {
    if (lo >= hi) {
        return lo;
    }
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}

void add_gaussian_blob(
    cv::Mat& buf_f32, const double cx, const double cy, const double peak, const double sigma) {
    const double window = 4.0 * sigma;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - window)));
    const int x1 = std::min(buf_f32.cols - 1, static_cast<int>(std::ceil(cx + window)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - window)));
    const int y1 = std::min(buf_f32.rows - 1, static_cast<int>(std::ceil(cy + window)));
    const double inv_two_sigma_sq = 1.0 / (2.0 * sigma * sigma);
    for (int y = y0; y <= y1; ++y) {
        auto* row = buf_f32.ptr<float>(y);
        const double dy = static_cast<double>(y) - cy;
        for (int x = x0; x <= x1; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double g = peak * std::exp(-(dx * dx + dy * dy) * inv_two_sigma_sq);
            row[x] = static_cast<float>(static_cast<double>(row[x]) + g);
        }
    }
}

void apply_background_gradient_and_vignette(
    cv::Mat& buf_f32, Rng& rng, const double gradient_amplitude, const double vignette_strength) {
    if (gradient_amplitude <= 0.0 && vignette_strength <= 0.0) {
        return;
    }
    const int w = buf_f32.cols;
    const int h = buf_f32.rows;
    const double angle = uniform(rng, 0.0, 2.0 * std::numbers::pi_v<double>);
    const double ca = std::cos(angle);
    const double sa = std::sin(angle);
    const double diag = std::hypot(static_cast<double>(w), static_cast<double>(h));
    const double cx = static_cast<double>(w) / 2.0;
    const double cy = static_cast<double>(h) / 2.0;
    const double rmax = std::hypot(cx, cy);

    for (int y = 0; y < h; ++y) {
        auto* row = buf_f32.ptr<float>(y);
        for (int x = 0; x < w; ++x) {
            double value = static_cast<double>(row[x]);
            if (gradient_amplitude > 0.0) {
                const double t = (static_cast<double>(x) * ca + static_cast<double>(y) * sa) / diag;
                value += gradient_amplitude * t;
            }
            if (vignette_strength > 0.0) {
                const double r = std::hypot(static_cast<double>(x) - cx, static_cast<double>(y) - cy) / rmax;
                value *= (1.0 - vignette_strength * r * r);
            }
            row[x] = static_cast<float>(value);
        }
    }
}

void apply_clutter(cv::Mat& buf_f32, Rng& rng, const DegradationConfig& config) {
    for (int i = 0; i < config.star_count; ++i) {
        const double sx = uniform(rng, 0.0, static_cast<double>(buf_f32.cols - 1));
        const double sy = uniform(rng, 0.0, static_cast<double>(buf_f32.rows - 1));
        const double sigma = uniform(rng, config.star_sigma_lo, config.star_sigma_hi);
        const double peak = uniform(rng, config.star_peak_lo, config.star_peak_hi);
        add_gaussian_blob(buf_f32, sx, sy, peak, sigma);
    }
    if (config.add_distractor) {
        const double dx = uniform(rng, 0.0, static_cast<double>(buf_f32.cols - 1));
        const double dy = uniform(rng, 0.0, static_cast<double>(buf_f32.rows - 1));
        const double sigma = uniform(rng, config.distractor_sigma_lo, config.distractor_sigma_hi);
        const double peak = uniform(rng, config.distractor_peak_lo, config.distractor_peak_hi);
        add_gaussian_blob(buf_f32, dx, dy, peak, sigma);
    }
}

[[nodiscard]] cv::Mat make_disk_kernel(const double radius) {
    const int r = std::max(1, static_cast<int>(std::lround(radius)));
    const int size = 2 * r + 1;
    cv::Mat kernel = cv::Mat::zeros(size, size, CV_32F);
    double sum = 0.0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double dx = static_cast<double>(x - r);
            const double dy = static_cast<double>(y - r);
            if (dx * dx + dy * dy <= static_cast<double>(r) * static_cast<double>(r)) {
                kernel.at<float>(y, x) = 1.0F;
                sum += 1.0;
            }
        }
    }
    if (sum > 0.0) {
        kernel /= static_cast<float>(sum);
    }
    return kernel;
}

[[nodiscard]] cv::Mat make_motion_kernel(const double length, const double angle_rad) {
    const int len = std::max(1, static_cast<int>(std::lround(length)));
    const int size = 2 * len + 1;
    cv::Mat kernel = cv::Mat::zeros(size, size, CV_32F);
    const double ca = std::cos(angle_rad);
    const double sa = std::sin(angle_rad);
    const int center = len;
    int count = 0;
    for (int t = -len; t <= len; ++t) {
        const int x = center + static_cast<int>(std::lround(static_cast<double>(t) * ca));
        const int y = center + static_cast<int>(std::lround(static_cast<double>(t) * sa));
        if (x >= 0 && x < size && y >= 0 && y < size) {
            kernel.at<float>(y, x) += 1.0F;
            ++count;
        }
    }
    if (count > 0) {
        kernel /= static_cast<float>(count);
    }
    return kernel;
}

void apply_optical_blur(cv::Mat& buf_f32, const DegradationConfig& config) {
    switch (config.optical_mode) {
        case OpticalMode::None:
            return;
        case OpticalMode::GaussianBlur: {
            cv::GaussianBlur(buf_f32, buf_f32, cv::Size(0, 0), config.optical_param);
            return;
        }
        case OpticalMode::Defocus: {
            const cv::Mat kernel = make_disk_kernel(config.optical_param);
            cv::filter2D(buf_f32, buf_f32, -1, kernel);
            return;
        }
        case OpticalMode::MotionBlur: {
            const cv::Mat kernel = make_motion_kernel(config.optical_param, config.optical_angle_rad);
            cv::filter2D(buf_f32, buf_f32, -1, kernel);
            return;
        }
    }
}

// Vectorized: a per-pixel std::normal_distribution loop over a 640x480 buffer
// (~307k samples) for every one of tens of thousands of closed-loop frames is
// the dominant cost of the whole Stage-4 benchmark; cv::RNG::fill draws the
// same i.i.d. Gaussian field in one call. The sub-seed is drawn from the
// existing per-frame `rng` stream, so the whole pipeline stays a pure,
// deterministic function of the caller's seed (still verified by
// tests/stage4_determinism_tests.cpp) even though the noise itself now comes
// from a different (still seeded, still deterministic) generator.
void apply_noise(cv::Mat& buf_f32, Rng& rng, const double read_sigma, const double shot_scale) {
    if (read_sigma <= 0.0 && shot_scale <= 0.0) {
        return;
    }
    const auto sub_seed = static_cast<std::uint64_t>(rng());
    cv::RNG cv_rng(static_cast<uint64>(sub_seed));

    if (shot_scale > 0.0) {
        cv::Mat shot_unit(buf_f32.size(), CV_32F);
        cv_rng.fill(shot_unit, cv::RNG::NORMAL, 0.0, 1.0);
        cv::Mat non_negative;
        cv::max(buf_f32, 0.0, non_negative);
        cv::Mat sqrt_val;
        cv::sqrt(non_negative, sqrt_val);
        buf_f32 += shot_unit.mul(sqrt_val) * shot_scale;
    }
    if (read_sigma > 0.0) {
        cv::Mat read_noise(buf_f32.size(), CV_32F);
        cv_rng.fill(read_noise, cv::RNG::NORMAL, 0.0, read_sigma);
        buf_f32 += read_noise;
    }
}

void apply_hot_dead_salt(cv::Mat& buf_f32, Rng& rng, const DegradationConfig& config) {
    if (config.hot_pixel_count <= 0 && config.dead_pixel_count <= 0 && config.salt_pixel_count <= 0) {
        return;
    }
    std::uniform_int_distribution<int> rx(0, buf_f32.cols - 1);
    std::uniform_int_distribution<int> ry(0, buf_f32.rows - 1);
    for (int i = 0; i < config.hot_pixel_count; ++i) {
        buf_f32.at<float>(ry(rng), rx(rng)) = static_cast<float>(uniform(rng, 245.0, 255.0));
    }
    for (int i = 0; i < config.dead_pixel_count; ++i) {
        buf_f32.at<float>(ry(rng), rx(rng)) = 0.0F;
    }
    for (int i = 0; i < config.salt_pixel_count; ++i) {
        buf_f32.at<float>(ry(rng), rx(rng)) = 255.0F;
    }
}

}  // namespace

cv::Mat apply_degradation(const cv::Mat& clean_u8, const std::uint64_t seed, const DegradationConfig& config) {
    if (clean_u8.empty()) {
        throw std::invalid_argument("stage4::apply_degradation: frame is empty.");
    }
    if (clean_u8.type() != CV_8UC1) {
        throw std::invalid_argument("stage4::apply_degradation: frame must be CV_8UC1.");
    }

    Rng rng(seed);

    cv::Mat buf_f32;
    clean_u8.convertTo(buf_f32, CV_32F);

    // Mirrors AiFrameSynthesizer's own frozen operation order (background ->
    // clutter -> optical blur -> noise -> hot/dead/salt -> clamp/round), but
    // as a post-process on an already-rendered image (see header comment).
    apply_background_gradient_and_vignette(buf_f32, rng, config.gradient_amplitude, config.vignette_strength);
    apply_clutter(buf_f32, rng, config);
    apply_optical_blur(buf_f32, config);
    apply_noise(buf_f32, rng, config.read_sigma, config.shot_scale);
    apply_hot_dead_salt(buf_f32, rng, config);

    cv::Mat clamped;
    cv::min(buf_f32, 255.0, clamped);
    cv::max(clamped, 0.0, clamped);
    cv::Mat out_u8;
    clamped.convertTo(out_u8, CV_8UC1);  // convertTo rounds-to-nearest for float->8U
    return out_u8;
}

}  // namespace fsoc::stage4
