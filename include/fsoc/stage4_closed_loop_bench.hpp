#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/stage4_common_frame_bench.hpp"  // HybridSourceCounts
#include "fsoc/stage4_scenarios.hpp"
#include "fsoc/target_tracker.hpp"

namespace fsoc::stage4 {

// ---------------------------------------------------------------------------
// Stage-4 closed-loop degraded benchmark (protocol §9/§10)
// ---------------------------------------------------------------------------
//
// A NEW, additive, evaluation-only loop -- it does NOT modify or reuse
// fsoc::SimulationRunner (Step 7, frozen). It mirrors that runner's own
// frozen step order exactly:
//   trajectory.state_at(t) -> observe_beacon -> renderer.render(observation)
//   -> [NEW: apply_degradation] -> detect (per mode) -> resolve_perception
//   -> compute_tracking_error -> pid.update / reset -> camera.step -> advance
// The one addition (apply_degradation) is why this cannot be SimulationRunner
// itself: that frozen step() has no degradation seam and this stage must not
// add one to it. Every primitive used (PanTiltCamera, StationaryTrajectory,
// observe_beacon, SyntheticCameraRenderer, BeaconDetector, AiBeaconDetector,
// resolve_perception, compute_tracking_error, PIDController, zero_control_command)
// is the SAME frozen production code the real runner uses -- nothing here
// reimplements projection, PID, or pixel->angle math.

struct ClosedLoopBenchConfig {
    double duration_s{8.0};
    double dt_s{0.02};
    std::size_t warmup_frames_for_latency{20};
};

// Per-(scenario, mode, seed) raw samples, pooled across seeds by
// finalize_closed_loop_results() before computing percentile-sensitive stats.
struct ClosedLoopRawSeedResult {
    std::size_t total_frames{0};
    std::size_t accepted_frames{0};
    std::size_t target_lost_frames{0};
    std::size_t longest_loss_streak{0};
    std::vector<double> reacquisition_times_frames;  // one entry per completed loss->tracking transition
    std::vector<double> angular_errors_rad;           // frames with a TrackingError
    std::size_t saturated_actuator_frames{0};
    double max_commanded_pan_rate_rad_s{0.0};
    double max_commanded_tilt_rate_rad_s{0.0};
    HybridSourceCounts hybrid_sources{};  // zero-filled for Classical/AI modes

    // Evaluator-only, truth-scored control-outlier safety metric (protocol §10/§11).
    std::size_t control_outlier_gt20{0};
    std::size_t control_outlier_gt50{0};
    std::size_t control_outlier_gt100{0};
    double max_control_error_px{0.0};

    std::vector<double> perception_latency_ms;  // excludes warmup_frames_for_latency
};

struct ClosedLoopScenarioModeResult {
    ScenarioId scenario{};
    fsoc::PerceptionMode mode{};

    std::size_t total_frames{0};
    double accepted_detection_fraction{0.0};
    std::size_t target_lost_frames{0};
    std::size_t longest_loss_streak{0};
    std::size_t reacquisition_count{0};
    double mean_reacquisition_time_frames{0.0};

    double rms_angular_error_rad{0.0};
    double median_angular_error_rad{0.0};
    double p95_angular_error_rad{0.0};
    double max_angular_error_rad{0.0};

    std::size_t saturated_actuator_frames{0};
    double max_commanded_pan_rate_rad_s{0.0};
    double max_commanded_tilt_rate_rad_s{0.0};

    HybridSourceCounts hybrid_sources{};

    std::size_t control_outlier_gt20{0};
    std::size_t control_outlier_gt50{0};
    std::size_t control_outlier_gt100{0};
    double max_control_error_px{0.0};

    double perception_latency_mean_ms{0.0};
    double perception_latency_p95_ms{0.0};
};

// Runs one (scenario, mode, seed) closed-loop simulation. `classical_detector`
// / `ai_detector` are constructed once by the caller (model loaded once) and
// reused across every call. Truth (`observation.image_point_px`,
// `target.position_m`) is used ONLY for the control-outlier safety metric and
// reacquisition/loss bookkeeping inside THIS evaluator -- it never reaches
// `classical_detector`, `ai_detector`, or `resolve_perception()`.
//
// `tracker_config` (P0-v2, Phase E/F, additive) -- default std::nullopt
// reproduces the EXACT pre-tracker loop bit-for-bit (see
// stage4_tracker_gate_bit_identical_when_disabled in
// tests/stage4_determinism_tests.cpp). When present, a fresh TargetTracker is
// constructed for this (scenario, mode, seed) run and layered AFTER
// resolve_perception() exactly as fsoc::SimulationRunner::step() does: the
// tracker consumes whatever resolve_perception() already accepted (Classical
// alone, or the Safe Hybrid fused candidate), and every downstream metric
// (accepted_frames, the truth-scored control-outlier counters, tracking_error)
// is computed from the TRACKER's gated output, not the raw perception output
// -- this is what actually reaches "control" in this evaluator, matching the
// real runner. `tracker_min_confidence_to_steer` is is_safe_to_steer()'s one
// threshold (see fsoc/target_tracker.hpp); ignored when tracker_config is
// std::nullopt.
[[nodiscard]] ClosedLoopRawSeedResult run_closed_loop_scenario_mode_seed(
    ScenarioId scenario,
    fsoc::PerceptionMode mode,
    std::uint64_t base_seed,
    std::size_t seed_index,
    const ClosedLoopBenchConfig& bench_config,
    const fsoc::BeaconDetector& classical_detector,
    const fsoc::AiBeaconDetector& ai_detector,
    std::optional<fsoc::TargetTrackerConfig> tracker_config = std::nullopt,
    double tracker_min_confidence_to_steer = 0.4);

// Pools raw per-seed samples and computes the final aggregate metrics
// (median/RMS/P95/max over the POOLED sample set, not an average of per-seed
// statistics).
[[nodiscard]] ClosedLoopScenarioModeResult finalize_closed_loop_results(
    ScenarioId scenario, fsoc::PerceptionMode mode, const std::vector<ClosedLoopRawSeedResult>& per_seed);

}  // namespace fsoc::stage4
