// Stage 3 — SimulationRunner perception-seam integration tests.
//
// Covers: (a) the mandatory Classical bit-identical regression (Stage-3 prompt
// §31) — default Classical vs explicit Classical, and the runner's
// control-facing detection vs a standalone BeaconDetector call on the
// byte-identical reconstructed frame; (b) SimulationRunnerConfig validation
// requiring an ai_detector config whenever perception_mode != Classical;
// (c) AI and Hybrid modes running end-to-end through the real runner + the
// real committed ONNX model, with diagnostics cross-checked against the
// ADR-018 table on every frame.
//
// Same lightweight harness as tests/step7_tests.cpp.

#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <opencv2/core.hpp>

#include "fsoc/detector.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/trajectory.hpp"

#ifndef FSOC_PROJECT_SOURCE_DIR
#error "FSOC_PROJECT_SOURCE_DIR must be defined by CMakeLists.txt"
#endif

namespace {

using fsoc::AiBeaconDetectorConfig;
using fsoc::BeaconDetector;
using fsoc::PerceptionMode;
using fsoc::PerceptionRejectionReason;
using fsoc::PerceptionSource;
using fsoc::SimulationRunner;
using fsoc::SimulationRunnerConfig;
using fsoc::SimulationStepResult;
using fsoc::StationaryTrajectory;
using fsoc::Vec3;
using fsoc::baseline_runner_config;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

template <typename Fn>
void check_throws_invalid_argument(Fn&& fn, const std::string_view expression, const int line) {
    bool threw = false;
    try {
        std::forward<Fn>(fn)();
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {
    }
    check(threw, expression, line);
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_THROWS_INVALID(expr) \
    check_throws_invalid_argument([&] { (void)(expr); }, #expr, __LINE__)

[[nodiscard]] std::string model_path() {
    return std::string(FSOC_PROJECT_SOURCE_DIR) + "/models/tiny_beacon_net.onnx";
}

[[nodiscard]] AiBeaconDetectorConfig ai_config() {
    AiBeaconDetectorConfig config{};
    config.model_path = model_path();
    config.presence_threshold = 0.95;
    return config;
}

// A target straight ahead on the world +X axis projects to the image centre
// under the baseline camera's initial (pan=0, tilt=0) pose (matches the Step
// 1/5 test convention: camera.project({100,0,0}) == (cx, cy)).
[[nodiscard]] StationaryTrajectory centred_target() {
    return StationaryTrajectory{Vec3{100.0, 0.0, 0.0}};
}

// ---- 1. explicit Classical config == default Classical config, bit-identical ----

void test_explicit_classical_matches_default() {
    const auto trajectory_a = centred_target();
    const auto trajectory_b = centred_target();

    SimulationRunnerConfig config_a = baseline_runner_config();  // perception_mode defaults Classical

    SimulationRunnerConfig config_b = baseline_runner_config();
    config_b.perception_mode = PerceptionMode::Classical;  // explicit, same value

    SimulationRunner runner_a{config_a, trajectory_a};
    SimulationRunner runner_b{config_b, trajectory_b};

    constexpr int kSteps = 25;
    for (int i = 0; i < kSteps; ++i) {
        const SimulationStepResult ra = runner_a.step();
        const SimulationStepResult rb = runner_b.step();

        CHECK(ra.simulation_time_s == rb.simulation_time_s);
        CHECK(ra.frame_index == rb.frame_index);
        CHECK(ra.target_detected == rb.target_detected);
        CHECK(ra.detection.has_value() == rb.detection.has_value());
        if (ra.detection.has_value() && rb.detection.has_value()) {
            CHECK(ra.detection->centroid_px.x_px == rb.detection->centroid_px.x_px);
            CHECK(ra.detection->centroid_px.y_px == rb.detection->centroid_px.y_px);
        }
        CHECK(ra.command.pan_rate_rad_s == rb.command.pan_rate_rad_s);
        CHECK(ra.command.tilt_rate_rad_s == rb.command.tilt_rate_rad_s);
        CHECK(ra.camera_pan_rad == rb.camera_pan_rad);
        CHECK(ra.camera_tilt_rad == rb.camera_tilt_rad);
        CHECK(ra.perception.perception_mode == PerceptionMode::Classical);
        CHECK(rb.perception.perception_mode == PerceptionMode::Classical);
    }
}

// ---- 2. Classical mode's control-facing detection == standalone BeaconDetector ----
//
// SimulationStepResult.rendered_frame is the EXACT frame the runner's own
// detector consumed this step (P0: stored on the result, not re-derived from
// `observation` — re-rendering is only guaranteed identical to the detector's
// input while rendering stays a pure, noise-free function of the observation).
// Running the SAME BeaconDetector config on that stored frame must reproduce
// the runner's control-facing detection exactly, proving Stage-3 integration
// did not perturb the Classical path.

void test_classical_detection_matches_standalone_detector() {
    SimulationRunnerConfig config = baseline_runner_config();
    const auto trajectory = centred_target();
    SimulationRunner runner{config, trajectory};

    const BeaconDetector detector{config.detector};

    constexpr int kSteps = 25;
    int compared = 0;
    for (int i = 0; i < kSteps; ++i) {
        const SimulationStepResult result = runner.step();

        const std::optional<fsoc::BeaconDetection> standalone = detector.detect(result.rendered_frame);

        CHECK(standalone.has_value() == result.detection.has_value());
        if (standalone.has_value() && result.detection.has_value()) {
            CHECK(standalone->centroid_px.x_px == result.detection->centroid_px.x_px);
            CHECK(standalone->centroid_px.y_px == result.detection->centroid_px.y_px);
            ++compared;
        }
    }
    CHECK(compared > 0);  // the comparison actually exercised detected frames
}

// ---- 3. ai_detector config is required whenever perception_mode != Classical ----

void test_ai_detector_config_required_for_non_classical() {
    SimulationRunnerConfig hybrid_missing_ai = baseline_runner_config();
    hybrid_missing_ai.perception_mode = PerceptionMode::Hybrid;
    // ai_detector left as std::nullopt.
    CHECK_THROWS_INVALID(hybrid_missing_ai.validate());
    {
        const auto trajectory = centred_target();
        CHECK_THROWS_INVALID(SimulationRunner(hybrid_missing_ai, trajectory));
    }

    SimulationRunnerConfig ai_missing = baseline_runner_config();
    ai_missing.perception_mode = PerceptionMode::AI;
    CHECK_THROWS_INVALID(ai_missing.validate());

    // With ai_detector supplied, validate() must NOT throw.
    SimulationRunnerConfig hybrid_ok = baseline_runner_config();
    hybrid_ok.perception_mode = PerceptionMode::Hybrid;
    hybrid_ok.ai_detector = ai_config();
    bool ok = true;
    try {
        hybrid_ok.validate();
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
}

// ---- 4. AI mode runs end-to-end through the real runner + real model ----

void test_ai_mode_runs_end_to_end() {
    SimulationRunnerConfig config = baseline_runner_config();
    config.perception_mode = PerceptionMode::AI;
    config.ai_detector = ai_config();

    const auto trajectory = centred_target();
    SimulationRunner runner{config, trajectory};

    constexpr int kSteps = 15;
    for (int i = 0; i < kSteps; ++i) {
        const SimulationStepResult result = runner.step();
        CHECK(result.perception.perception_mode == PerceptionMode::AI);
        CHECK(!result.perception.classical_detected);  // classical never runs in AI mode
        CHECK(result.perception.ai_candidate_detected == result.detection.has_value());
        if (result.perception.ai_candidate_detected) {
            CHECK(result.perception.ai_presence_probability.has_value());
            CHECK(*result.perception.ai_presence_probability >= 0.95);  // frozen threshold
            CHECK(result.perception.ai_inference_ms.has_value());
            CHECK(*result.perception.ai_inference_ms >= 0.0);
            CHECK(result.perception.perception_source == PerceptionSource::AI);
        } else {
            CHECK(result.perception.perception_source == PerceptionSource::None);
        }
    }
}

// ---- 5. Hybrid mode runs end-to-end; diagnostics match the ADR-018 table ----

void test_hybrid_mode_runs_end_to_end_and_matches_adr018_table() {
    SimulationRunnerConfig config = baseline_runner_config();
    config.perception_mode = PerceptionMode::Hybrid;
    config.ai_detector = ai_config();

    const auto trajectory = centred_target();
    SimulationRunner runner{config, trajectory};

    constexpr int kSteps = 15;
    for (int i = 0; i < kSteps; ++i) {
        const SimulationStepResult result = runner.step();
        CHECK(result.perception.perception_mode == PerceptionMode::Hybrid);

        // ADR-018: PerceptionSource::AI must NEVER be emitted under Hybrid.
        CHECK(result.perception.perception_source != PerceptionSource::AI);

        const bool classical_ok = result.perception.classical_detected;
        const bool ai_ok = result.perception.ai_candidate_detected;

        if (classical_ok && ai_ok) {
            CHECK(result.perception.classical_ai_distance_px.has_value());
            const bool agree = *result.perception.classical_ai_distance_px <= fsoc::kAgreementRadiusPx;
            if (agree) {
                CHECK(result.perception.perception_source == PerceptionSource::HybridAgreement);
                CHECK(result.perception.rejection_reason == PerceptionRejectionReason::NotApplicable);
                CHECK(result.detection.has_value());
            } else {
                CHECK(result.perception.perception_source == PerceptionSource::None);
                CHECK(result.perception.rejection_reason == PerceptionRejectionReason::DetectorDisagreement);
                CHECK(!result.detection.has_value());
            }
        } else if (classical_ok) {
            CHECK(result.perception.perception_source == PerceptionSource::Classical);
            CHECK(result.detection.has_value());
        } else if (ai_ok) {
            CHECK(result.perception.perception_source == PerceptionSource::None);
            CHECK(result.perception.rejection_reason == PerceptionRejectionReason::AiOnlyUnverified);
            CHECK(!result.detection.has_value());  // no control authority for AI-only (ADR-018)
        } else {
            CHECK(result.perception.perception_source == PerceptionSource::None);
            CHECK(result.perception.rejection_reason == PerceptionRejectionReason::NotApplicable);
            CHECK(!result.detection.has_value());
        }
    }
}

}  // namespace

int main() {
    test_explicit_classical_matches_default();
    test_classical_detection_matches_standalone_detector();
    test_ai_detector_config_required_for_non_classical();
    test_ai_mode_runs_end_to_end();
    test_hybrid_mode_runs_end_to_end_and_matches_adr018_table();

    if (failures == 0) {
        std::cout << "PASS: Stage-3 perception-seam integration checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
