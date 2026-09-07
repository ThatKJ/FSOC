// V2 AI PERCEPTION — deterministic unit checks for the synthetic dataset
// generator (fsoc::ai::AiFrameSynthesizer + seeding helpers).
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/step11_tests.cpp.
// No frozen v1_baseline module is exercised here.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include "fsoc/ai_frame_synth.hpp"

namespace {

using fsoc::ai::AiFrameSynthConfig;
using fsoc::ai::AiFrameSynthesizer;
using fsoc::ai::sample_seed_for;
using fsoc::ai::splitmix64;
using fsoc::ai::SynthFrame;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

template <typename Fn>
void check_throws(Fn&& fn, const std::string_view expression, const int line) {
    bool threw = false;
    try {
        std::forward<Fn>(fn)();
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {
        // wrong exception type — still a failure
    }
    if (!threw) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << " (expected std::invalid_argument)\n";
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_THROWS(expr) check_throws([&] { expr; }, #expr, __LINE__)

// A degradation-free config: dark pedestal + analytic beacon only. Makes the
// beacon-presence / negative-emptiness assertions exact.
[[nodiscard]] AiFrameSynthConfig clean_config() {
    AiFrameSynthConfig c{};
    c.width_px = 128;
    c.height_px = 96;
    c.negative_fraction = 0.5;
    c.beacon.peak_intensity = {200.0, 200.0};
    c.beacon.sigma_px = {2.0, 2.0};
    c.beacon.anisotropy_ratio = {1.0, 1.0};
    c.beacon.elongation_probability = 0.0;
    c.beacon.max_edge_overshoot_px = 0.0;
    c.background.dark_offset = {5.0, 5.0};
    c.background.gradient_amplitude = {0.0, 0.0};
    c.background.vignette_strength = {0.0, 0.0};
    c.noise.read_sigma = {0.0, 0.0};
    c.noise.shot_scale = {0.0, 0.0};
    c.noise.hot_pixel_count = {0.0, 0.0};
    c.noise.dead_pixel_count = {0.0, 0.0};
    c.noise.salt_pixel_count = {0.0, 0.0};
    c.optical.weight_none = 1.0;
    c.optical.weight_gaussian_blur = 0.0;
    c.optical.weight_defocus = 0.0;
    c.optical.weight_motion_blur = 0.0;
    c.clutter.star_count = {0.0, 0.0};
    c.clutter.bright_distractor_probability = 0.0;
    c.clutter.cluster_probability = 0.0;
    return c;
}

void test_config_validation() {
    CHECK_THROWS((void)AiFrameSynthesizer(([] {
        AiFrameSynthConfig c{};
        c.width_px = 0;
        return c;
    }())));
    CHECK_THROWS((void)AiFrameSynthesizer(([] {
        AiFrameSynthConfig c{};
        c.negative_fraction = 1.5;
        return c;
    }())));
    CHECK_THROWS((void)AiFrameSynthesizer(([] {
        AiFrameSynthConfig c{};
        c.beacon.sigma_px = {5.0, 1.0};  // lo > hi
        return c;
    }())));
    CHECK_THROWS((void)AiFrameSynthesizer(([] {
        AiFrameSynthConfig c{};
        c.background.vignette_strength = {0.0, 1.0};  // hi must be < 1
        return c;
    }())));
    // default config must be valid
    try {
        AiFrameSynthesizer synth{AiFrameSynthConfig{}};
        CHECK(synth.config().width_px == 640);
    } catch (...) {
        CHECK(false);
    }
}

void test_seed_helpers() {
    CHECK(splitmix64(0) != 0);
    CHECK(sample_seed_for(26169, 7) == sample_seed_for(26169, 7));   // deterministic
    CHECK(sample_seed_for(26169, 7) != sample_seed_for(26169, 8));   // index-sensitive
    CHECK(sample_seed_for(1, 7) != sample_seed_for(2, 7));           // dataset-seed-sensitive

    // No seed collisions across a realistic index range -> no split leakage via
    // colliding sample seeds (splits are contiguous disjoint index blocks).
    std::set<std::uint64_t> seen;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        seen.insert(sample_seed_for(26169, static_cast<std::uint64_t>(i)));
    }
    CHECK(static_cast<int>(seen.size()) == n);
}

void test_frame_format_and_determinism() {
    const AiFrameSynthesizer synth{AiFrameSynthConfig{}};  // full domain randomization
    const std::uint64_t seed = sample_seed_for(26169, 123);

    const SynthFrame a = synth.synthesize(seed);
    const SynthFrame b = synth.synthesize(seed);

    CHECK(a.image.type() == CV_8UC1);
    CHECK(a.image.cols == 640 && a.image.rows == 480);
    // byte-identical for equal seeds
    CHECK(cv::norm(a.image, b.image, cv::NORM_INF) == 0.0);
    CHECK(a.target_present == b.target_present);
    CHECK(a.x_px == b.x_px && a.y_px == b.y_px);
    CHECK(a.params.optical_mode == b.params.optical_mode);

    // a different seed almost surely yields a different frame
    const SynthFrame c = synth.synthesize(sample_seed_for(26169, 124));
    CHECK(cv::norm(a.image, c.image, cv::NORM_INF) != 0.0);
}

void test_force_target_override() {
    const AiFrameSynthesizer synth{AiFrameSynthConfig{}};
    const std::uint64_t seed = sample_seed_for(7, 7);
    CHECK(synth.synthesize(seed, true).target_present == true);
    CHECK(synth.synthesize(seed, false).target_present == false);
}

void test_positive_has_signal_negative_is_empty() {
    const AiFrameSynthesizer synth{clean_config()};

    for (int i = 0; i < 40; ++i) {
        const std::uint64_t seed = sample_seed_for(999, static_cast<std::uint64_t>(i));

        const SynthFrame pos = synth.synthesize(seed, true);
        CHECK(pos.target_present);
        CHECK(std::isfinite(pos.x_px) && std::isfinite(pos.y_px));
        CHECK(pos.x_px >= 0.0 && pos.x_px <= 127.0);
        CHECK(pos.y_px >= 0.0 && pos.y_px <= 95.0);
        const int px = static_cast<int>(std::lround(pos.x_px));
        const int py = static_cast<int>(std::lround(pos.y_px));
        CHECK(pos.image.at<std::uint8_t>(py, px) > 100);   // beacon core is bright
        CHECK(pos.image.at<std::uint8_t>(0, 0) < 20);       // corner is pedestal only

        const SynthFrame neg = synth.synthesize(seed, false);
        CHECK(!neg.target_present);
        double max_v = 0.0;
        cv::minMaxLoc(neg.image, nullptr, &max_v);
        CHECK(max_v < 20.0);   // no beacon, no clutter, no noise -> flat pedestal
    }
}

void test_negative_fraction_is_respected() {
    AiFrameSynthConfig c{};
    c.width_px = 64;   // small frames: this test only counts the present/absent draw
    c.height_px = 48;
    c.negative_fraction = 0.30;
    const AiFrameSynthesizer synth{c};

    int negatives = 0;
    const int n = 2400;
    for (int i = 0; i < n; ++i) {
        const SynthFrame f = synth.synthesize(sample_seed_for(26169, static_cast<std::uint64_t>(i)));
        if (!f.target_present) {
            ++negatives;
        }
    }
    const double frac = static_cast<double>(negatives) / static_cast<double>(n);
    CHECK(std::abs(frac - 0.30) < 0.045);
}

void test_edge_clipping_occurs_both_ways() {
    AiFrameSynthConfig c{};
    c.width_px = 96;
    c.height_px = 72;
    c.beacon.max_edge_overshoot_px = 6.0;
    const AiFrameSynthesizer synth{c};

    int clipped = 0;
    int interior = 0;
    for (int i = 0; i < 250; ++i) {
        const SynthFrame f =
            synth.synthesize(sample_seed_for(555, static_cast<std::uint64_t>(i)), true);
        if (f.params.beacon_edge_clipped) {
            ++clipped;
        } else {
            ++interior;
        }
    }
    CHECK(clipped > 0);    // partial-clip edge cases are generated
    CHECK(interior > 0);   // and so are ordinary interior beacons
}

void test_difficulty_proxy_bounds() {
    AiFrameSynthConfig c{};
    c.width_px = 48;
    c.height_px = 36;
    const AiFrameSynthesizer synth{c};
    for (int i = 0; i < 120; ++i) {
        const SynthFrame f = synth.synthesize(sample_seed_for(3, static_cast<std::uint64_t>(i)));
        CHECK(f.params.difficulty >= 0.0 && f.params.difficulty <= 1.0);
        CHECK(std::isfinite(f.params.difficulty));
    }
}

}  // namespace

int main() {
    test_config_validation();
    test_seed_helpers();
    test_frame_format_and_determinism();
    test_force_target_override();
    test_positive_has_signal_negative_is_empty();
    test_negative_fraction_is_respected();
    test_edge_clipping_occurs_both_ways();
    test_difficulty_proxy_bounds();

    if (failures == 0) {
        std::cout << "ai_datagen_tests: all checks passed\n";
        return 0;
    }
    std::cerr << "ai_datagen_tests: " << failures << " check(s) failed\n";
    return 1;
}
