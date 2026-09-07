#include "fsoc/ai_frame_synth.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

#include <opencv2/imgproc.hpp>

namespace fsoc::ai {

// ---------------------------------------------------------------------------
// Config validation
// ---------------------------------------------------------------------------

void Range::validate(const char* field) const {
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
        throw std::invalid_argument(std::string("AiFrameSynthConfig: ") + field +
                                    " range bounds must be finite.");
    }
    if (lo > hi) {
        throw std::invalid_argument(std::string("AiFrameSynthConfig: ") + field +
                                    " range requires lo <= hi.");
    }
}

void BeaconSynthConfig::validate() const {
    peak_intensity.validate("beacon.peak_intensity");
    sigma_px.validate("beacon.sigma_px");
    anisotropy_ratio.validate("beacon.anisotropy_ratio");
    rotation_rad.validate("beacon.rotation_rad");
    if (sigma_px.lo <= 0.0) {
        throw std::invalid_argument("AiFrameSynthConfig: beacon.sigma_px.lo must be > 0.");
    }
    if (anisotropy_ratio.lo < 1.0) {
        throw std::invalid_argument("AiFrameSynthConfig: beacon.anisotropy_ratio.lo must be >= 1.");
    }
    if (!(elongation_probability >= 0.0 && elongation_probability <= 1.0)) {
        throw std::invalid_argument(
            "AiFrameSynthConfig: beacon.elongation_probability must be in [0, 1].");
    }
    if (!std::isfinite(max_edge_overshoot_px) || max_edge_overshoot_px < 0.0) {
        throw std::invalid_argument(
            "AiFrameSynthConfig: beacon.max_edge_overshoot_px must be finite and >= 0.");
    }
}

void BackgroundSynthConfig::validate() const {
    dark_offset.validate("background.dark_offset");
    gradient_amplitude.validate("background.gradient_amplitude");
    vignette_strength.validate("background.vignette_strength");
    if (dark_offset.lo < 0.0) {
        throw std::invalid_argument("AiFrameSynthConfig: background.dark_offset.lo must be >= 0.");
    }
    if (vignette_strength.lo < 0.0 || vignette_strength.hi >= 1.0) {
        throw std::invalid_argument(
            "AiFrameSynthConfig: background.vignette_strength must be in [0, 1).");
    }
}

void NoiseSynthConfig::validate() const {
    read_sigma.validate("noise.read_sigma");
    shot_scale.validate("noise.shot_scale");
    hot_pixel_count.validate("noise.hot_pixel_count");
    dead_pixel_count.validate("noise.dead_pixel_count");
    salt_pixel_count.validate("noise.salt_pixel_count");
    if (read_sigma.lo < 0.0 || shot_scale.lo < 0.0) {
        throw std::invalid_argument(
            "AiFrameSynthConfig: noise.read_sigma / noise.shot_scale lo must be >= 0.");
    }
    if (hot_pixel_count.lo < 0.0 || dead_pixel_count.lo < 0.0 || salt_pixel_count.lo < 0.0) {
        throw std::invalid_argument("AiFrameSynthConfig: noise pixel counts must be >= 0.");
    }
}

void OpticalSynthConfig::validate() const {
    gaussian_blur_sigma_px.validate("optical.gaussian_blur_sigma_px");
    defocus_radius_px.validate("optical.defocus_radius_px");
    motion_blur_length_px.validate("optical.motion_blur_length_px");
    motion_blur_angle_rad.validate("optical.motion_blur_angle_rad");
    const double sum = weight_none + weight_gaussian_blur + weight_defocus + weight_motion_blur;
    if (!(sum > 0.0) || !std::isfinite(sum)) {
        throw std::invalid_argument("AiFrameSynthConfig: optical weights must sum to a positive value.");
    }
    if (weight_none < 0.0 || weight_gaussian_blur < 0.0 || weight_defocus < 0.0 ||
        weight_motion_blur < 0.0) {
        throw std::invalid_argument("AiFrameSynthConfig: optical weights must be >= 0.");
    }
}

void ClutterSynthConfig::validate() const {
    star_count.validate("clutter.star_count");
    star_peak_intensity.validate("clutter.star_peak_intensity");
    star_sigma_px.validate("clutter.star_sigma_px");
    bright_distractor_excess.validate("clutter.bright_distractor_excess");
    if (star_count.lo < 0.0) {
        throw std::invalid_argument("AiFrameSynthConfig: clutter.star_count.lo must be >= 0.");
    }
    if (star_sigma_px.lo <= 0.0) {
        throw std::invalid_argument("AiFrameSynthConfig: clutter.star_sigma_px.lo must be > 0.");
    }
    if (!(bright_distractor_probability >= 0.0 && bright_distractor_probability <= 1.0)) {
        throw std::invalid_argument(
            "AiFrameSynthConfig: clutter.bright_distractor_probability must be in [0, 1].");
    }
    if (!(cluster_probability >= 0.0 && cluster_probability <= 1.0)) {
        throw std::invalid_argument(
            "AiFrameSynthConfig: clutter.cluster_probability must be in [0, 1].");
    }
}

void AiFrameSynthConfig::validate() const {
    if (width_px <= 0 || height_px <= 0) {
        throw std::invalid_argument("AiFrameSynthConfig: width_px and height_px must be > 0.");
    }
    if (!(negative_fraction >= 0.0 && negative_fraction <= 1.0)) {
        throw std::invalid_argument("AiFrameSynthConfig: negative_fraction must be in [0, 1].");
    }
    beacon.validate();
    background.validate();
    noise.validate();
    optical.validate();
    clutter.validate();
}

// ---------------------------------------------------------------------------
// Seeding
// ---------------------------------------------------------------------------

std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::uint64_t sample_seed_for(const std::uint64_t dataset_seed, const std::uint64_t index) noexcept {
    return splitmix64(dataset_seed ^ splitmix64(index + 0x1234567ULL));
}

// ---------------------------------------------------------------------------
// Local RNG helpers (all randomness routed through std::mt19937_64)
// ---------------------------------------------------------------------------

namespace {

using Rng = std::mt19937_64;

[[nodiscard]] double uniform(Rng& rng, const double lo, const double hi) {
    if (lo == hi) {
        return lo;
    }
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}

[[nodiscard]] double uniform(Rng& rng, const Range& r) { return uniform(rng, r.lo, r.hi); }

[[nodiscard]] int uniform_int_round(Rng& rng, const Range& r) {
    return static_cast<int>(std::lround(uniform(rng, r.lo, r.hi)));
}

[[nodiscard]] double gaussian(Rng& rng, const double mean, const double sigma) {
    if (sigma <= 0.0) {
        return mean;
    }
    return std::normal_distribution<double>(mean, sigma)(rng);
}

// Reusable standard-normal draw. Hoisted out of pixel loops so the per-frame
// noise pass does not construct a distribution object per pixel (both a large
// speed win for full-size dataset frames and a stable RNG draw sequence).
[[nodiscard]] double std_normal(Rng& rng, std::normal_distribution<double>& unit) {
    return unit(rng);
}

[[nodiscard]] bool chance(Rng& rng, const double p) {
    if (p <= 0.0) {
        return false;
    }
    if (p >= 1.0) {
        return true;
    }
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < p;
}

// Add an (optionally anisotropic, rotated) 2-D Gaussian splat into a CV_32F
// buffer. Evaluated over a clipped window so a near/off-edge centre is safe.
void add_gaussian(
    cv::Mat& buf,
    const double cx,
    const double cy,
    const double peak,
    const double sigma_major,
    const double sigma_minor,
    const double theta_rad) {
    const int w = buf.cols;
    const int h = buf.rows;
    const double radius = 4.0 * std::max(sigma_major, sigma_minor);
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + radius)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + radius)));
    if (x0 > x1 || y0 > y1) {
        return;  // splat lies fully outside the image
    }

    const double ct = std::cos(theta_rad);
    const double st = std::sin(theta_rad);
    const double inv_2a = 1.0 / (2.0 * sigma_major * sigma_major);
    const double inv_2b = 1.0 / (2.0 * sigma_minor * sigma_minor);

    for (int y = y0; y <= y1; ++y) {
        auto* row = buf.ptr<float>(y);
        const double dy = static_cast<double>(y) - cy;
        for (int x = x0; x <= x1; ++x) {
            const double dx = static_cast<double>(x) - cx;
            // rotate the offset into the ellipse's principal axes
            const double u = dx * ct + dy * st;
            const double v = -dx * st + dy * ct;
            const double g = peak * std::exp(-(u * u * inv_2a + v * v * inv_2b));
            row[x] += static_cast<float>(g);
        }
    }
}

// Normalized linear motion-blur kernel of the given length and angle.
[[nodiscard]] cv::Mat motion_kernel(const double length_px, const double angle_rad) {
    const int len = std::max(3, static_cast<int>(std::lround(length_px)) | 1);  // odd, >= 3
    cv::Mat kernel = cv::Mat::zeros(len, len, CV_32F);
    const double c = (len - 1) / 2.0;
    const double dx = std::cos(angle_rad);
    const double dy = std::sin(angle_rad);
    for (int i = 0; i < len; ++i) {
        const double t = static_cast<double>(i) - c;
        const int x = static_cast<int>(std::lround(c + t * dx));
        const int y = static_cast<int>(std::lround(c + t * dy));
        if (x >= 0 && x < len && y >= 0 && y < len) {
            kernel.at<float>(y, x) += 1.0F;
        }
    }
    const double s = cv::sum(kernel)[0];
    if (s > 0.0) {
        kernel /= s;
    }
    return kernel;
}

// Normalized filled-disk kernel of the given radius (defocus / bokeh).
[[nodiscard]] cv::Mat defocus_kernel(const double radius_px) {
    const int r = std::max(1, static_cast<int>(std::ceil(radius_px)));
    const int size = 2 * r + 1;
    cv::Mat kernel = cv::Mat::zeros(size, size, CV_32F);
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (static_cast<double>(x * x + y * y) <= radius_px * radius_px) {
                kernel.at<float>(y + r, x + r) = 1.0F;
            }
        }
    }
    const double s = cv::sum(kernel)[0];
    if (s > 0.0) {
        kernel /= s;
    }
    return kernel;
}

[[nodiscard]] double clamp01(const double v) { return std::clamp(v, 0.0, 1.0); }

}  // namespace

// ---------------------------------------------------------------------------
// AiFrameSynthesizer
// ---------------------------------------------------------------------------

AiFrameSynthesizer::AiFrameSynthesizer(AiFrameSynthConfig config) : config_(std::move(config)) {
    config_.validate();
}

SynthFrame AiFrameSynthesizer::synthesize(
    const std::uint64_t sample_seed,
    const std::optional<bool> force_target) const {
    Rng rng(sample_seed);

    const int w = config_.width_px;
    const int h = config_.height_px;
    SynthFrame out{};
    DegradationSample& p = out.params;

    const bool target_present =
        force_target.has_value() ? *force_target : !chance(rng, config_.negative_fraction);
    p.target_present = target_present;
    out.target_present = target_present;

    // --- stage 1: pedestal ------------------------------------------------
    p.dark_offset = uniform(rng, config_.background.dark_offset);
    cv::Mat buf(h, w, CV_32F, cv::Scalar(p.dark_offset));

    // --- stage 2: gradient plane + radial vignette -----------------------
    p.gradient_amplitude = uniform(rng, config_.background.gradient_amplitude);
    p.vignette_strength = uniform(rng, config_.background.vignette_strength);
    const double grad_angle = uniform(rng, 0.0, 2.0 * std::numbers::pi_v<double>);
    const double gx = std::cos(grad_angle);
    const double gy = std::sin(grad_angle);
    const double cx_img = (w - 1) / 2.0;
    const double cy_img = (h - 1) / 2.0;
    const double r2_norm = 1.0 / (cx_img * cx_img + cy_img * cy_img + 1e-9);
    for (int y = 0; y < h; ++y) {
        auto* row = buf.ptr<float>(y);
        const double ny = static_cast<double>(y) / static_cast<double>(h) - 0.5;
        const double dvy = static_cast<double>(y) - cy_img;
        for (int x = 0; x < w; ++x) {
            const double nx = static_cast<double>(x) / static_cast<double>(w) - 0.5;
            const double plane = p.gradient_amplitude * (nx * gx + ny * gy);
            const double dvx = static_cast<double>(x) - cx_img;
            const double vig = 1.0 - p.vignette_strength * ((dvx * dvx + dvy * dvy) * r2_norm);
            row[x] = static_cast<float>((static_cast<double>(row[x]) + plane) * vig);
        }
    }

    // --- stage 3: analytic Gaussian beacon (positive samples only) -------
    double beacon_peak = 0.0;
    if (target_present) {
        const double over = config_.beacon.max_edge_overshoot_px;
        out.x_px = uniform(rng, -over, static_cast<double>(w - 1) + over);
        out.y_px = uniform(rng, -over, static_cast<double>(h - 1) + over);
        beacon_peak = uniform(rng, config_.beacon.peak_intensity);
        const double sigma = uniform(rng, config_.beacon.sigma_px);
        double sigma_major = sigma;
        double sigma_minor = sigma;
        double theta = 0.0;
        if (chance(rng, config_.beacon.elongation_probability)) {
            const double ratio = uniform(rng, config_.beacon.anisotropy_ratio);
            sigma_major = sigma * std::sqrt(ratio);
            sigma_minor = sigma / std::sqrt(ratio);
            theta = uniform(rng, config_.beacon.rotation_rad);
        }
        add_gaussian(buf, out.x_px, out.y_px, beacon_peak, sigma_major, sigma_minor, theta);

        p.beacon_peak = beacon_peak;
        p.beacon_sigma_px = sigma;
        p.beacon_anisotropy = sigma_major / sigma_minor;
        p.beacon_rotation_rad = theta;
        p.beacon_edge_clipped = out.x_px < 0.0 || out.x_px > static_cast<double>(w - 1) ||
                                out.y_px < 0.0 || out.y_px > static_cast<double>(h - 1);
    }

    // --- stage 4: star-like clutter + optional bright distractor ---------
    p.star_count = uniform_int_round(rng, config_.clutter.star_count);
    const bool want_bright = chance(rng, config_.clutter.bright_distractor_probability);
    const double reference_peak =
        target_present ? beacon_peak : config_.clutter.star_peak_intensity.hi;
    double brightest_distractor = 0.0;
    for (int i = 0; i < p.star_count; ++i) {
        const double sx = uniform(rng, 0.0, static_cast<double>(w - 1));
        const double sy = uniform(rng, 0.0, static_cast<double>(h - 1));
        const double ssigma = uniform(rng, config_.clutter.star_sigma_px);
        double speak = uniform(rng, config_.clutter.star_peak_intensity);
        if (want_bright && i == 0) {
            speak = std::min(255.0,
                             reference_peak + uniform(rng, config_.clutter.bright_distractor_excess));
        }
        add_gaussian(buf, sx, sy, speak, ssigma, ssigma, 0.0);
        brightest_distractor = std::max(brightest_distractor, speak);
    }
    p.has_bright_distractor = want_bright && p.star_count > 0;
    p.brightest_distractor_peak = brightest_distractor;

    p.has_cluster = chance(rng, config_.clutter.cluster_probability);
    if (p.has_cluster) {
        const double base_x = uniform(rng, 0.0, static_cast<double>(w - 1));
        const double base_y = uniform(rng, 0.0, static_cast<double>(h - 1));
        const int members = 2 + static_cast<int>(std::lround(uniform(rng, 0.0, 2.0)));
        for (int i = 0; i < members; ++i) {
            const double jx = base_x + gaussian(rng, 0.0, 2.5);
            const double jy = base_y + gaussian(rng, 0.0, 2.5);
            const double ssigma = uniform(rng, config_.clutter.star_sigma_px);
            const double speak = uniform(rng, config_.clutter.star_peak_intensity);
            add_gaussian(buf, jx, jy, speak, ssigma, ssigma, 0.0);
        }
    }

    // --- stage 5: one optical-degradation operator ----------------------
    {
        const OpticalSynthConfig& oc = config_.optical;
        const double total = oc.weight_none + oc.weight_gaussian_blur + oc.weight_defocus +
                             oc.weight_motion_blur;
        double pick = uniform(rng, 0.0, total);
        if ((pick -= oc.weight_none) < 0.0) {
            p.optical_mode = "none";
        } else if ((pick -= oc.weight_gaussian_blur) < 0.0) {
            p.optical_mode = "gaussian_blur";
            p.optical_param = uniform(rng, oc.gaussian_blur_sigma_px);
            cv::GaussianBlur(buf, buf, cv::Size(0, 0), p.optical_param, p.optical_param,
                             cv::BORDER_REPLICATE);
        } else if ((pick -= oc.weight_defocus) < 0.0) {
            p.optical_mode = "defocus";
            p.optical_param = uniform(rng, oc.defocus_radius_px);
            cv::filter2D(buf, buf, -1, defocus_kernel(p.optical_param), cv::Point(-1, -1), 0.0,
                         cv::BORDER_REPLICATE);
        } else {
            p.optical_mode = "motion_blur";
            p.optical_param = uniform(rng, oc.motion_blur_length_px);
            p.optical_angle_rad = uniform(rng, oc.motion_blur_angle_rad);
            cv::filter2D(buf, buf, -1, motion_kernel(p.optical_param, p.optical_angle_rad),
                         cv::Point(-1, -1), 0.0, cv::BORDER_REPLICATE);
        }
    }

    // --- stage 6: shot-like noise then Gaussian read noise --------------
    p.shot_scale = uniform(rng, config_.noise.shot_scale);
    p.read_sigma = uniform(rng, config_.noise.read_sigma);
    {
        std::normal_distribution<double> unit(0.0, 1.0);
        const bool do_shot = p.shot_scale > 0.0;
        const bool do_read = p.read_sigma > 0.0;
        if (do_shot || do_read) {
            for (int y = 0; y < h; ++y) {
                auto* row = buf.ptr<float>(y);
                for (int x = 0; x < w; ++x) {
                    double val = static_cast<double>(row[x]);
                    if (do_shot && val > 0.0) {
                        // Gaussian approximation to Poisson: variance == signal.
                        val += p.shot_scale * std::sqrt(val) * std_normal(rng, unit);
                    }
                    if (do_read) {
                        val += p.read_sigma * std_normal(rng, unit);
                    }
                    row[x] = static_cast<float>(val);
                }
            }
        }
    }

    // --- stage 7: hot / dead / salt pixels -----------------------------
    p.hot_pixels = uniform_int_round(rng, config_.noise.hot_pixel_count);
    p.dead_pixels = uniform_int_round(rng, config_.noise.dead_pixel_count);
    p.salt_pixels = uniform_int_round(rng, config_.noise.salt_pixel_count);
    std::uniform_int_distribution<int> rx(0, w - 1);
    std::uniform_int_distribution<int> ry(0, h - 1);
    for (int i = 0; i < p.hot_pixels; ++i) {
        buf.at<float>(ry(rng), rx(rng)) = static_cast<float>(uniform(rng, 245.0, 255.0));
    }
    for (int i = 0; i < p.dead_pixels; ++i) {
        buf.at<float>(ry(rng), rx(rng)) = 0.0F;
    }
    for (int i = 0; i < p.salt_pixels; ++i) {
        buf.at<float>(ry(rng), rx(rng)) = 255.0F;
    }

    // --- stage 8: clamp + quantize to CV_8UC1 -------------------------
    out.image.create(h, w, CV_8UC1);
    for (int y = 0; y < h; ++y) {
        const auto* src = buf.ptr<float>(y);
        auto* dst = out.image.ptr<std::uint8_t>(y);
        for (int x = 0; x < w; ++x) {
            const double v = std::clamp(static_cast<double>(src[x]), 0.0, 255.0);
            dst[x] = static_cast<std::uint8_t>(std::lround(v));
        }
    }

    // --- difficulty proxy (stratified reporting only; never a label) ---
    const double snr = target_present ? beacon_peak / std::max(p.read_sigma, 1.0) : 0.0;
    const double d_snr = target_present ? clamp01(1.0 - snr / 40.0) : 0.30;
    const double d_noise = clamp01(p.read_sigma / config_.noise.read_sigma.hi);
    const double d_optical =
        p.optical_mode == "none" ? 0.0 : clamp01(p.optical_param / 4.0);
    const double d_clutter =
        config_.clutter.star_count.hi > 0.0
            ? clamp01(static_cast<double>(p.star_count) / config_.clutter.star_count.hi)
            : 0.0;
    const double d_distract = p.has_bright_distractor ? 1.0 : 0.0;
    p.difficulty = clamp01(0.30 * d_snr + 0.20 * d_noise + 0.20 * d_optical + 0.15 * d_clutter +
                           0.15 * d_distract);

    return out;
}

}  // namespace fsoc::ai
