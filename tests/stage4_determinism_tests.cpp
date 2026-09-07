// Stage 4 — determinism checks for the evaluation tooling itself.
//
// Protocol requirement (docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md §6): random
// degradation must depend ONLY on (scenario, base_seed, frame_index) -- never
// on detector output, controller output, or mode -- so results are repeatable
// and comparable across Classical/AI/Hybrid runs of the same scenario/seed.
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/ai_beacon_detector_tests.cpp.

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "fsoc/stage4_closed_loop_bench.hpp"
#include "fsoc/stage4_common_frame_bench.hpp"
#include "fsoc/stage4_degradation.hpp"
#include "fsoc/stage4_scenarios.hpp"

#ifndef FSOC_PROJECT_SOURCE_DIR
#error "FSOC_PROJECT_SOURCE_DIR must be defined by CMakeLists.txt"
#endif

namespace {

using fsoc::AiBeaconDetector;
using fsoc::AiBeaconDetectorConfig;
using fsoc::BeaconDetector;
using fsoc::BeaconDetectorConfig;
using fsoc::PerceptionMode;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

[[nodiscard]] AiBeaconDetectorConfig ai_config() {
    AiBeaconDetectorConfig config{};
    config.model_path = std::string(FSOC_PROJECT_SOURCE_DIR) + "/models/tiny_beacon_net.onnx";
    config.presence_threshold = 0.95;
    return config;
}

// ---- 1. frame_seed / scenario_stream_seed are pure functions ----------

void test_frame_seed_is_pure() {
    for (const auto scenario : fsoc::stage4::kAllScenarios) {
        const int idx = fsoc::stage4::scenario_index(scenario);
        for (const std::uint64_t base_seed : fsoc::stage4::kStage4BaseSeeds) {
            const std::uint64_t a = fsoc::stage4::frame_seed(idx, base_seed, 0);
            const std::uint64_t b = fsoc::stage4::frame_seed(idx, base_seed, 0);
            CHECK(a == b);
            const std::uint64_t c = fsoc::stage4::frame_seed(idx, base_seed, 1);
            CHECK(a != c);  // different frame index -> different seed (no accidental collision)
        }
    }
}

// ---- 2. apply_degradation is deterministic -----------------------------

void test_apply_degradation_is_deterministic() {
    const cv::Mat clean(480, 640, CV_8UC1, cv::Scalar(5.0));
    fsoc::stage4::DegradationConfig config{};
    config.read_sigma = 4.0;
    config.shot_scale = 0.3;
    config.hot_pixel_count = 8;
    config.gradient_amplitude = 12.0;
    config.vignette_strength = 0.15;
    config.star_count = 3;
    config.optical_mode = fsoc::stage4::OpticalMode::GaussianBlur;
    config.optical_param = 1.0;

    const cv::Mat a = fsoc::stage4::apply_degradation(clean, 12345ULL, config);
    const cv::Mat b = fsoc::stage4::apply_degradation(clean, 12345ULL, config);
    CHECK(a.size() == b.size());
    bool identical = true;
    for (int y = 0; y < a.rows && identical; ++y) {
        const auto* ra = a.ptr<std::uint8_t>(y);
        const auto* rb = b.ptr<std::uint8_t>(y);
        for (int x = 0; x < a.cols; ++x) {
            if (ra[x] != rb[x]) {
                identical = false;
                break;
            }
        }
    }
    CHECK(identical);

    // A different seed must (almost certainly) change the output.
    const cv::Mat c = fsoc::stage4::apply_degradation(clean, 999ULL, config);
    bool any_diff = false;
    for (int y = 0; y < a.rows && !any_diff; ++y) {
        const auto* ra = a.ptr<std::uint8_t>(y);
        const auto* rc = c.ptr<std::uint8_t>(y);
        for (int x = 0; x < a.cols; ++x) {
            if (ra[x] != rc[x]) {
                any_diff = true;
                break;
            }
        }
    }
    CHECK(any_diff);
}

// ---- 3. common-frame scenario benchmark is deterministic ----------------

void test_common_frame_scenario_is_deterministic() {
    const BeaconDetector classical{BeaconDetectorConfig{}};
    const AiBeaconDetector ai{ai_config()};

    const std::array<std::uint64_t, 5> seeds = {fsoc::stage4::kStage4BaseSeeds[0]};
    const auto r1 =
        fsoc::stage4::run_common_frame_scenario(fsoc::stage4::ScenarioId::StarClutter, fsoc::stage4::kStage4BaseSeeds, 20, classical, ai);
    const auto r2 =
        fsoc::stage4::run_common_frame_scenario(fsoc::stage4::ScenarioId::StarClutter, fsoc::stage4::kStage4BaseSeeds, 20, classical, ai);
    (void)seeds;

    CHECK(r1.total_frames == r2.total_frames);
    CHECK(r1.classical.true_positive == r2.classical.true_positive);
    CHECK(r1.classical.false_positive == r2.classical.false_positive);
    CHECK(r1.ai.true_positive == r2.ai.true_positive);
    CHECK(r1.ai.false_positive == r2.ai.false_positive);
    CHECK(r1.hybrid.true_positive == r2.hybrid.true_positive);
    CHECK(r1.hybrid.localization.median_px == r2.hybrid.localization.median_px);
    CHECK(r1.hybrid_sources.hybrid_agreement == r2.hybrid_sources.hybrid_agreement);
    CHECK(r1.hybrid_sources.detector_disagreement == r2.hybrid_sources.detector_disagreement);
    CHECK(r1.safety.disagreement_would_be_bad_gt50 == r2.safety.disagreement_would_be_bad_gt50);
}

// ---- 4. closed-loop benchmark is deterministic, per (scenario, mode, seed) --

void test_closed_loop_scenario_is_deterministic() {
    const BeaconDetector classical{BeaconDetectorConfig{}};
    const AiBeaconDetector ai{ai_config()};
    fsoc::stage4::ClosedLoopBenchConfig bench{};
    bench.duration_s = 2.0;  // short run for the test
    bench.warmup_frames_for_latency = 5;

    const auto r1 = fsoc::stage4::run_closed_loop_scenario_mode_seed(
        fsoc::stage4::ScenarioId::HotPixels, PerceptionMode::Hybrid, fsoc::stage4::kStage4BaseSeeds[0], 0, bench,
        classical, ai);
    const auto r2 = fsoc::stage4::run_closed_loop_scenario_mode_seed(
        fsoc::stage4::ScenarioId::HotPixels, PerceptionMode::Hybrid, fsoc::stage4::kStage4BaseSeeds[0], 0, bench,
        classical, ai);

    CHECK(r1.total_frames == r2.total_frames);
    CHECK(r1.accepted_frames == r2.accepted_frames);
    CHECK(r1.target_lost_frames == r2.target_lost_frames);
    CHECK(r1.longest_loss_streak == r2.longest_loss_streak);
    CHECK(r1.hybrid_sources.hybrid_agreement == r2.hybrid_sources.hybrid_agreement);
    CHECK(r1.hybrid_sources.detector_disagreement == r2.hybrid_sources.detector_disagreement);
    CHECK(r1.hybrid_sources.ai_only_unverified == r2.hybrid_sources.ai_only_unverified);
    CHECK(r1.angular_errors_rad.size() == r2.angular_errors_rad.size());
    for (std::size_t i = 0; i < r1.angular_errors_rad.size(); ++i) {
        CHECK(r1.angular_errors_rad[i] == r2.angular_errors_rad[i]);
    }
    CHECK(r1.max_commanded_pan_rate_rad_s == r2.max_commanded_pan_rate_rad_s);
}

// ---- 5. degradation seed stream does not depend on mode ------------------
// (the closed-loop degraded frame at a given frame_index only depends on
//  scenario+base_seed+frame_index; two different modes on the same
//  scenario/seed must therefore see byte-identical clean-frame-before-control-
//  diverges, verified indirectly here via identical frame_seed() outputs for
//  every frame_index in a short run's range).

void test_degradation_seed_independent_of_mode() {
    const int idx = fsoc::stage4::scenario_index(fsoc::stage4::ScenarioId::Clean);
    const std::uint64_t base_seed = fsoc::stage4::kStage4BaseSeeds[0];
    for (std::uint64_t frame_index = 0; frame_index < 10; ++frame_index) {
        const std::uint64_t seed_call_1 = fsoc::stage4::frame_seed(idx, base_seed, frame_index);
        const std::uint64_t seed_call_2 = fsoc::stage4::frame_seed(idx, base_seed, frame_index);
        CHECK(seed_call_1 == seed_call_2);
    }
}

// ---- 6. closed-loop tracker seam (P0-v2 Phase E/F): disabled is bit-
//         identical to omitted, enabled is deterministic and materially
//         changes the outcome on a false-lock-prone scenario -------------

void test_closed_loop_tracker_seam() {
    const BeaconDetector classical{BeaconDetectorConfig{}};
    const AiBeaconDetector ai{ai_config()};
    fsoc::stage4::ClosedLoopBenchConfig bench{};
    bench.duration_s = 4.0;
    bench.warmup_frames_for_latency = 5;

    const auto baseline = fsoc::stage4::run_closed_loop_scenario_mode_seed(
        fsoc::stage4::ScenarioId::BrightDistractor, PerceptionMode::Classical,
        fsoc::stage4::kStage4BaseSeeds[0], 0, bench, classical, ai);
    const auto explicit_disabled = fsoc::stage4::run_closed_loop_scenario_mode_seed(
        fsoc::stage4::ScenarioId::BrightDistractor, PerceptionMode::Classical,
        fsoc::stage4::kStage4BaseSeeds[0], 0, bench, classical, ai, std::nullopt, 0.4);

    CHECK(baseline.accepted_frames == explicit_disabled.accepted_frames);
    CHECK(baseline.control_outlier_gt50 == explicit_disabled.control_outlier_gt50);
    CHECK(baseline.max_control_error_px == explicit_disabled.max_control_error_px);

    const fsoc::TargetTrackerConfig tracker_cfg{};  // defaults
    const auto with_tracker_1 = fsoc::stage4::run_closed_loop_scenario_mode_seed(
        fsoc::stage4::ScenarioId::BrightDistractor, PerceptionMode::Classical,
        fsoc::stage4::kStage4BaseSeeds[0], 0, bench, classical, ai, tracker_cfg, 0.4);
    const auto with_tracker_2 = fsoc::stage4::run_closed_loop_scenario_mode_seed(
        fsoc::stage4::ScenarioId::BrightDistractor, PerceptionMode::Classical,
        fsoc::stage4::kStage4BaseSeeds[0], 0, bench, classical, ai, tracker_cfg, 0.4);

    // Determinism holds with the tracker enabled too.
    CHECK(with_tracker_1.accepted_frames == with_tracker_2.accepted_frames);
    CHECK(with_tracker_1.control_outlier_gt50 == with_tracker_2.control_outlier_gt50);
    CHECK(with_tracker_1.max_control_error_px == with_tracker_2.max_control_error_px);

    // The tracker's temporal-consistency gate must materially reduce the
    // worst-case truth-scored control error on a scenario designed to
    // false-lock Classical onto spatially-random clutter (BrightDistractor's
    // distractor is re-randomized across the whole frame every frame,
    // independent of the beacon -- see stage4_degradation.cpp apply_clutter)
    // -- if it didn't, the seam would be dead code.
    CHECK(with_tracker_1.max_control_error_px < baseline.max_control_error_px);
}

}  // namespace

int main() {
    test_frame_seed_is_pure();
    test_apply_degradation_is_deterministic();
    test_common_frame_scenario_is_deterministic();
    test_closed_loop_scenario_is_deterministic();
    test_degradation_seed_independent_of_mode();
    test_closed_loop_tracker_seam();

    if (failures == 0) {
        std::cout << "PASS: Stage-4 determinism checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
