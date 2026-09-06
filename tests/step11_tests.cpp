// Step 11 deterministic unit checks: demo packaging layer (DemoScenario /
// DemoSnapshot / DemoSession) over the FROZEN v1_baseline engine.
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/step10_tests.cpp.
// Links fsoc::demo_support + fsoc::validation (transitively simulation /
// telemetry / visualization + OpenCV).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/camera.hpp"
#include "fsoc/config.hpp"
#include "fsoc/demo.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/telemetry.hpp"
#include "fsoc/trajectory.hpp"
#include "fsoc/validation.hpp"

#ifndef FSOC_PROJECT_SOURCE_DIR
#error "FSOC_PROJECT_SOURCE_DIR must be defined by CMakeLists.txt"
#endif

namespace {

using fsoc::DemoRunState;
using fsoc::DemoScenario;
using fsoc::DemoScenarioPlan;
using fsoc::DemoSession;
using fsoc::DemoSnapshot;
using fsoc::DemoTrackingState;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

// ---- shared helpers -------------------------------------------------

[[nodiscard]] bool opt_eq(const std::optional<double>& a, const std::optional<double>& b) {
    return a.has_value() == b.has_value() && (!a.has_value() || *a == *b);
}

[[nodiscard]] bool snapshots_equal(const DemoSnapshot& a, const DemoSnapshot& b) {
    return a.simulation_time_s == b.simulation_time_s && a.frame_index == b.frame_index &&
           a.state == b.state && a.target.x_m == b.target.x_m && a.target.y_m == b.target.y_m &&
           a.target.z_m == b.target.z_m && a.target.vx_mps == b.target.vx_mps &&
           a.target.vy_mps == b.target.vy_mps && a.target.vz_mps == b.target.vz_mps &&
           a.camera.pan_rad == b.camera.pan_rad && a.camera.tilt_rad == b.camera.tilt_rad &&
           a.camera.pan_rate_rad_s == b.camera.pan_rate_rad_s &&
           a.camera.tilt_rate_rad_s == b.camera.tilt_rate_rad_s &&
           a.camera.horizontal_fov_rad == b.camera.horizontal_fov_rad &&
           a.camera.vertical_fov_rad == b.camera.vertical_fov_rad &&
           a.detection.detected == b.detection.detected &&
           opt_eq(a.detection.x_px, b.detection.x_px) &&
           opt_eq(a.detection.y_px, b.detection.y_px) &&
           opt_eq(a.tracking.error_x_px, b.tracking.error_x_px) &&
           opt_eq(a.tracking.error_y_px, b.tracking.error_y_px) &&
           opt_eq(a.tracking.pan_error_rad, b.tracking.pan_error_rad) &&
           opt_eq(a.tracking.tilt_error_rad, b.tracking.tilt_error_rad) &&
           opt_eq(a.tracking.total_error_rad, b.tracking.total_error_rad) &&
           a.control.command_pan_rate_rad_s == b.control.command_pan_rate_rad_s &&
           a.control.command_tilt_rate_rad_s == b.control.command_tilt_rate_rad_s &&
           a.control.pan_saturated == b.control.pan_saturated &&
           a.control.tilt_saturated == b.control.tilt_saturated;
}

[[nodiscard]] bool step_results_equal(const fsoc::SimulationStepResult& a,
                                      const fsoc::SimulationStepResult& b) {
    if (a.simulation_time_s != b.simulation_time_s) return false;
    if (a.frame_index != b.frame_index) return false;
    if (a.camera_pan_rad != b.camera_pan_rad) return false;
    if (a.camera_tilt_rad != b.camera_tilt_rad) return false;
    if (a.command.pan_rate_rad_s != b.command.pan_rate_rad_s) return false;
    if (a.command.tilt_rate_rad_s != b.command.tilt_rate_rad_s) return false;
    if (a.applied_rates.pan_rate_rad_s != b.applied_rates.pan_rate_rad_s) return false;
    if (a.applied_rates.tilt_rate_rad_s != b.applied_rates.tilt_rate_rad_s) return false;
    if (a.target_detected != b.target_detected) return false;
    if (a.detection.has_value() != b.detection.has_value()) return false;
    if (a.detection.has_value() &&
        (a.detection->centroid_px.x_px != b.detection->centroid_px.x_px ||
         a.detection->centroid_px.y_px != b.detection->centroid_px.y_px)) {
        return false;
    }
    if (a.tracking_error.has_value() != b.tracking_error.has_value()) return false;
    if (a.tracking_error.has_value() &&
        (a.tracking_error->angular.pan_rad != b.tracking_error->angular.pan_rad ||
         a.tracking_error->angular.tilt_rad != b.tracking_error->angular.tilt_rad)) {
        return false;
    }
    return a.target_truth.position_m.x == b.target_truth.position_m.x &&
           a.target_truth.position_m.y == b.target_truth.position_m.y &&
           a.target_truth.position_m.z == b.target_truth.position_m.z;
}

struct SessionRun {
    std::vector<DemoSnapshot> snapshots;
    std::vector<fsoc::TelemetryRecord> telemetry;
    std::vector<fsoc::SimulationStepResult> steps;
};

[[nodiscard]] SessionRun run_session(const DemoScenario scenario) {
    DemoSession session{scenario};
    SessionRun out;
    out.snapshots.reserve(session.total_frames());
    while (!session.finished()) {
        out.snapshots.push_back(session.step());
        out.telemetry.push_back(session.last_telemetry());
        out.steps.push_back(session.last_step_result());
    }
    return out;
}

struct FrameTriplet {
    fsoc::SimulationStepResult result;
    fsoc::TelemetryRecord telemetry;
    DemoSnapshot snapshot;
};

// Independent bare-runner frame: build the same plan the DemoSession would, step
// a plain SimulationRunner to `frame`, and convert with the Step-8/Step-11
// helpers. Used to prove the demo layer copies rather than recomputes.
[[nodiscard]] FrameTriplet frame_at(const DemoScenario scenario, const std::size_t frame) {
    DemoScenarioPlan plan = fsoc::make_demo_scenario_plan(scenario);
    fsoc::SimulationRunner runner{plan.runner_config, *plan.trajectory};
    fsoc::SimulationStepResult result{};
    for (std::size_t i = 0; i <= frame; ++i) {
        result = runner.step();
    }
    const fsoc::TelemetryRecord telemetry = fsoc::make_telemetry_record(
        result, plan.runner_config.camera.max_pan_rate_rad_s,
        plan.runner_config.camera.max_tilt_rate_rad_s);
    const DemoSnapshot snapshot =
        fsoc::make_demo_snapshot(result, telemetry, plan.runner_config.camera);
    return {result, telemetry, snapshot};
}

[[nodiscard]] std::size_t first_lost_frame(const SessionRun& run) {
    for (std::size_t i = 0; i < run.snapshots.size(); ++i) {
        if (run.snapshots[i].state == DemoTrackingState::TargetLost) {
            return i;
        }
    }
    return run.snapshots.size();
}

// ===================================================================
// 1. make_demo_snapshot copies target coordinates + clock correctly
// ===================================================================

void test_snapshot_copies_target() {
    const FrameTriplet f = frame_at(DemoScenario::SinusoidalTracking, 37);
    CHECK(f.snapshot.target.x_m == f.result.target_truth.position_m.x);
    CHECK(f.snapshot.target.y_m == f.result.target_truth.position_m.y);
    CHECK(f.snapshot.target.z_m == f.result.target_truth.position_m.z);
    CHECK(f.snapshot.target.vx_mps == f.result.target_truth.velocity_mps.x);
    CHECK(f.snapshot.target.vy_mps == f.result.target_truth.velocity_mps.y);
    CHECK(f.snapshot.target.vz_mps == f.result.target_truth.velocity_mps.z);
    CHECK(f.snapshot.simulation_time_s == f.result.simulation_time_s);
    CHECK(f.snapshot.frame_index == f.result.frame_index);
    CHECK(f.snapshot.frame_index == 37u);
}

// ===================================================================
// 2. camera pan/tilt + applied rate + FOV copied correctly
// ===================================================================

void test_snapshot_camera() {
    const FrameTriplet f = frame_at(DemoScenario::SinusoidalTracking, 60);
    CHECK(f.snapshot.camera.pan_rad == f.result.camera_pan_rad);
    CHECK(f.snapshot.camera.tilt_rad == f.result.camera_tilt_rad);
    CHECK(f.snapshot.camera.pan_rate_rad_s == f.result.applied_rates.pan_rate_rad_s);
    CHECK(f.snapshot.camera.tilt_rate_rad_s == f.result.applied_rates.tilt_rate_rad_s);
    CHECK(f.snapshot.camera.horizontal_fov_rad == fsoc::CameraConfig{}.hfov_rad);
    CHECK(f.snapshot.camera.vertical_fov_rad == fsoc::CameraConfig{}.vfov_rad);
}

// ===================================================================
// 3. detected centroid optional fields present when tracking
// ===================================================================

void test_detection_present_when_tracking() {
    const FrameTriplet f = frame_at(DemoScenario::StaticAcquisition, 0);
    CHECK(f.snapshot.detection.detected);
    CHECK(f.snapshot.detection.x_px.has_value());
    CHECK(f.snapshot.detection.y_px.has_value());
    if (f.snapshot.detection.x_px.has_value() && f.telemetry.detected_x_px.has_value()) {
        CHECK(*f.snapshot.detection.x_px == *f.telemetry.detected_x_px);
    }
    CHECK(f.snapshot.tracking.total_error_rad.has_value());
    CHECK(f.snapshot.tracking.pan_error_rad.has_value());
}

// ===================================================================
// 4 / 5 / 6. centroid + error fields absent when lost; state correct
// ===================================================================

void test_snapshot_absent_when_lost() {
    const SessionRun run = run_session(DemoScenario::LossReacquisition);
    const std::size_t idx = first_lost_frame(run);
    CHECK(idx < run.snapshots.size());
    if (idx >= run.snapshots.size()) {
        return;
    }
    const DemoSnapshot& s = run.snapshots[idx];
    CHECK(s.state == DemoTrackingState::TargetLost);   // (6)
    CHECK(!s.detection.detected);
    CHECK(!s.detection.x_px.has_value());              // (4)
    CHECK(!s.detection.y_px.has_value());
    CHECK(!s.tracking.error_x_px.has_value());         // (5)
    CHECK(!s.tracking.error_y_px.has_value());
    CHECK(!s.tracking.pan_error_rad.has_value());
    CHECK(!s.tracking.tilt_error_rad.has_value());
    CHECK(!s.tracking.total_error_rad.has_value());
    CHECK(s.control.command_pan_rate_rad_s == 0.0);
    CHECK(s.control.command_tilt_rate_rad_s == 0.0);
}

// ===================================================================
// 6. tracking-state mapping is 1:1 with the actual system state
// ===================================================================

void test_tracking_state_mapping() {
    CHECK(fsoc::to_demo_tracking_state(fsoc::TrackingState::Tracking) ==
          DemoTrackingState::Tracking);
    CHECK(fsoc::to_demo_tracking_state(fsoc::TrackingState::TargetLost) ==
          DemoTrackingState::TargetLost);
    CHECK(fsoc::to_string(DemoTrackingState::Tracking) == "TRACKING");
    CHECK(fsoc::to_string(DemoTrackingState::TargetLost) == "TARGET_LOST");

    const FrameTriplet tracking = frame_at(DemoScenario::StaticAcquisition, 0);
    CHECK(tracking.snapshot.state ==
          fsoc::to_demo_tracking_state(tracking.telemetry.tracking_state));
    CHECK(tracking.snapshot.state == DemoTrackingState::Tracking);
}

// ===================================================================
// 7. command + saturation fields copied correctly
// ===================================================================

void test_snapshot_command_and_saturation() {
    const FrameTriplet f = frame_at(DemoScenario::StaticAcquisition, 0);
    CHECK(f.snapshot.control.command_pan_rate_rad_s == f.result.command.pan_rate_rad_s);
    CHECK(f.snapshot.control.command_tilt_rate_rad_s == f.result.command.tilt_rate_rad_s);
    CHECK(f.snapshot.control.pan_saturated == f.telemetry.pan_saturated);
    CHECK(f.snapshot.control.tilt_saturated == f.telemetry.tilt_saturated);
    // Static acquisition begins with the actuator flat out at the rate limit.
    CHECK(f.snapshot.control.pan_saturated || f.snapshot.control.tilt_saturated);
}

// ===================================================================
// 8. core stays radians (never silently converted to degrees)
// ===================================================================

void test_core_is_radians() {
    const SessionRun run = run_session(DemoScenario::SinusoidalTracking);
    CHECK(!run.snapshots.empty());
    const DemoSnapshot& first = run.snapshots.front();
    CHECK(std::abs(first.camera.horizontal_fov_rad - fsoc::deg_to_rad(20.0)) < 1e-12);
    CHECK(first.camera.horizontal_fov_rad < 1.0);   // 20 deg -> ~0.349 rad, not 20
    CHECK(std::abs(first.camera.vertical_fov_rad - fsoc::deg_to_rad(15.0)) < 1e-12);

    bool errors_small_radians = true;
    for (const DemoSnapshot& s : run.snapshots) {
        if (s.tracking.total_error_rad.has_value()) {
            errors_small_radians = errors_small_radians && *s.tracking.total_error_rad < 0.1;
        }
    }
    CHECK(errors_small_radians);   // sinusoidal max is ~0.8 deg = ~0.014 rad
}

// ===================================================================
// 9. to_degrees() conversion helper is correct
// ===================================================================

void test_to_degrees_helper() {
    const FrameTriplet f = frame_at(DemoScenario::StaticAcquisition, 0);
    const fsoc::DemoSnapshotAnglesDeg d = fsoc::to_degrees(f.snapshot);
    CHECK(std::abs(d.camera_pan_deg - fsoc::rad_to_deg(f.snapshot.camera.pan_rad)) < 1e-12);
    CHECK(std::abs(d.camera_tilt_deg - fsoc::rad_to_deg(f.snapshot.camera.tilt_rad)) < 1e-12);
    CHECK(std::abs(d.horizontal_fov_deg - 20.0) < 1e-9);
    CHECK(std::abs(d.vertical_fov_deg - 15.0) < 1e-9);
    CHECK(d.total_error_deg.has_value() == f.snapshot.tracking.total_error_rad.has_value());
    if (d.total_error_deg.has_value() && f.snapshot.tracking.total_error_rad.has_value()) {
        CHECK(std::abs(*d.total_error_deg -
                       fsoc::rad_to_deg(*f.snapshot.tracking.total_error_rad)) < 1e-12);
    }

    const SessionRun loss = run_session(DemoScenario::LossReacquisition);
    const std::size_t idx = first_lost_frame(loss);
    CHECK(idx < loss.snapshots.size());
    if (idx < loss.snapshots.size()) {
        CHECK(!fsoc::to_degrees(loss.snapshots[idx]).total_error_deg.has_value());
    }
}

// ===================================================================
// 10 / 11. DemoSession static: off-axis start, then convergence
// ===================================================================

void test_session_static_starts_offaxis() {
    DemoSession session{DemoScenario::StaticAcquisition};
    const DemoSnapshot s0 = session.step();
    CHECK(s0.frame_index == 0u);
    CHECK(s0.state == DemoTrackingState::Tracking);
    CHECK(s0.detection.detected);
    CHECK(s0.tracking.total_error_rad.has_value());
    if (s0.tracking.total_error_rad.has_value()) {
        CHECK(fsoc::rad_to_deg(*s0.tracking.total_error_rad) > 2.0);   // ~4.13 deg
    }
}

void test_session_static_converges() {
    const SessionRun run = run_session(DemoScenario::StaticAcquisition);
    CHECK(run.snapshots.size() == 200u);
    CHECK(!run.snapshots.empty());
    const DemoSnapshot& last = run.snapshots.back();
    CHECK(last.state == DemoTrackingState::Tracking);
    CHECK(last.tracking.total_error_rad.has_value());
    if (last.tracking.total_error_rad.has_value()) {
        CHECK(fsoc::rad_to_deg(*last.tracking.total_error_rad) < 0.05);
    }
}

// ===================================================================
// 12. DemoSession sinusoidal stays inside the Step-10 validated band
// ===================================================================

void test_session_sinusoidal_in_validated_ranges() {
    const SessionRun run = run_session(DemoScenario::SinusoidalTracking);
    CHECK(run.snapshots.size() == 1000u);
    const fsoc::BenchmarkMetrics m = fsoc::compute_benchmark_metrics(run.telemetry, 0.0);
    CHECK(m.detection_fraction >= 0.95);
    CHECK(m.lost_frames == 0u);
    CHECK(fsoc::rad_to_deg(m.rms_angular_error_rad) < 1.0);
    CHECK(fsoc::rad_to_deg(m.p95_angular_error_rad) < 1.0);
    CHECK(fsoc::rad_to_deg(m.max_angular_error_rad) < 1.5);
}

// ===================================================================
// 13 / 14. loss scenario: produces a lost state, then reacquires
// ===================================================================

void test_session_loss_and_reacquire() {
    const SessionRun run = run_session(DemoScenario::LossReacquisition);
    CHECK(run.snapshots.size() == 400u);

    const fsoc::BenchmarkMetrics m = fsoc::compute_benchmark_metrics(run.telemetry, 0.0);
    CHECK(m.lost_frames > 0u);

    const std::size_t lost_idx = first_lost_frame(run);
    CHECK(lost_idx < run.snapshots.size());   // (13) lost state occurs

    bool reacquired = false;
    for (std::size_t i = lost_idx + 1; i < run.snapshots.size(); ++i) {
        if (run.snapshots[i].state == DemoTrackingState::Tracking) {
            reacquired = true;
            break;
        }
    }
    CHECK(reacquired);   // (14) natural re-entry
}

// ===================================================================
// 15 / 16 / 17. open vs closed: control flag + identical trajectory
// ===================================================================

void test_open_controller_disabled() {
    DemoSession session{DemoScenario::OpenLoop};
    CHECK(session.runner_config().control_enabled == false);

    const SessionRun run = run_session(DemoScenario::OpenLoop);
    const double pan0 = run.snapshots.front().camera.pan_rad;
    bool commands_zero = true;
    bool pan_never_moves = true;
    for (const DemoSnapshot& s : run.snapshots) {
        commands_zero = commands_zero && s.control.command_pan_rate_rad_s == 0.0 &&
                        s.control.command_tilt_rate_rad_s == 0.0;
        pan_never_moves = pan_never_moves && s.camera.pan_rad == pan0;
    }
    CHECK(commands_zero);
    CHECK(pan_never_moves);
}

void test_closed_controller_enabled() {
    DemoSession session{DemoScenario::ClosedLoop};
    CHECK(session.runner_config().control_enabled == true);

    const SessionRun run = run_session(DemoScenario::ClosedLoop);
    bool pan_moves = false;
    for (const DemoSnapshot& s : run.snapshots) {
        if (s.camera.pan_rad != 0.0) {
            pan_moves = true;
            break;
        }
    }
    CHECK(pan_moves);
}

void test_open_closed_same_trajectory() {
    DemoScenarioPlan open_plan = fsoc::make_demo_scenario_plan(DemoScenario::OpenLoop);
    DemoScenarioPlan closed_plan = fsoc::make_demo_scenario_plan(DemoScenario::ClosedLoop);

    CHECK(open_plan.runner_config.control_enabled == false);
    CHECK(closed_plan.runner_config.control_enabled == true);
    CHECK(open_plan.duration_s == closed_plan.duration_s);
    CHECK(open_plan.runner_config.initial_tilt_rad == closed_plan.runner_config.initial_tilt_rad);

    const auto* open_sin =
        dynamic_cast<const fsoc::SinusoidalTrajectory*>(open_plan.trajectory.get());
    const auto* closed_sin =
        dynamic_cast<const fsoc::SinusoidalTrajectory*>(closed_plan.trajectory.get());
    CHECK(open_sin != nullptr);
    CHECK(closed_sin != nullptr);
    if (open_sin != nullptr && closed_sin != nullptr) {
        const fsoc::SinusoidalTrajectory::Parameters& po = open_sin->parameters();
        const fsoc::SinusoidalTrajectory::Parameters& pc = closed_sin->parameters();
        CHECK(po.center_position_m.x == pc.center_position_m.x);
        CHECK(po.center_position_m.y == pc.center_position_m.y);
        CHECK(po.center_position_m.z == pc.center_position_m.z);
        CHECK(po.amplitude_m.x == pc.amplitude_m.x);
        CHECK(po.amplitude_m.y == pc.amplitude_m.y);
        CHECK(po.amplitude_m.z == pc.amplitude_m.z);
        CHECK(po.frequency_hz.x == pc.frequency_hz.x);
        CHECK(po.frequency_hz.y == pc.frequency_hz.y);
        CHECK(po.frequency_hz.z == pc.frequency_hz.z);
        CHECK(po.phase_rad.x == pc.phase_rad.x);
        CHECK(po.phase_rad.y == pc.phase_rad.y);
        CHECK(po.phase_rad.z == pc.phase_rad.z);
    }

    // Behavioural: identical target-truth sequence frame for frame.
    const SessionRun open_run = run_session(DemoScenario::OpenLoop);
    const SessionRun closed_run = run_session(DemoScenario::ClosedLoop);
    CHECK(open_run.steps.size() == closed_run.steps.size());
    bool truth_identical = open_run.steps.size() == closed_run.steps.size();
    for (std::size_t i = 0; i < open_run.steps.size() && truth_identical; ++i) {
        const fsoc::Vec3& a = open_run.steps[i].target_truth.position_m;
        const fsoc::Vec3& b = closed_run.steps[i].target_truth.position_m;
        truth_identical = a.x == b.x && a.y == b.y && a.z == b.z;
    }
    CHECK(truth_identical);
}

// ===================================================================
// 18. reset -> deterministic replay
// ===================================================================

void test_reset_replay() {
    DemoSession session{DemoScenario::SinusoidalTracking};
    const std::vector<DemoSnapshot> first = session.run_to_completion();
    CHECK(session.finished());

    session.reset();
    CHECK(session.run_state() == DemoRunState::Ready);
    CHECK(session.frame_index() == 0u);
    CHECK(session.simulation_time_s() == 0.0);

    const std::vector<DemoSnapshot> second = session.run_to_completion();
    CHECK(first.size() == second.size());
    bool identical = first.size() == second.size();
    for (std::size_t i = 0; i < first.size() && identical; ++i) {
        identical = snapshots_equal(first[i], second[i]);
    }
    CHECK(identical);
}

// ===================================================================
// 19. two sessions of one scenario -> identical snapshot sequences
// ===================================================================

void test_two_sessions_identical() {
    const SessionRun a = run_session(DemoScenario::LossReacquisition);
    const SessionRun b = run_session(DemoScenario::LossReacquisition);
    CHECK(a.snapshots.size() == b.snapshots.size());
    bool identical = a.snapshots.size() == b.snapshots.size();
    for (std::size_t i = 0; i < a.snapshots.size() && identical; ++i) {
        identical = snapshots_equal(a.snapshots[i], b.snapshots[i]);
    }
    CHECK(identical);
}

// ===================================================================
// 20. invalid scenario string rejected cleanly by the parser/helper
// ===================================================================

void test_parse_scenario() {
    CHECK(fsoc::parse_demo_scenario("static") == DemoScenario::StaticAcquisition);
    CHECK(fsoc::parse_demo_scenario("sinusoidal") == DemoScenario::SinusoidalTracking);
    CHECK(fsoc::parse_demo_scenario("loss") == DemoScenario::LossReacquisition);
    CHECK(fsoc::parse_demo_scenario("open") == DemoScenario::OpenLoop);
    CHECK(fsoc::parse_demo_scenario("closed") == DemoScenario::ClosedLoop);
    CHECK(fsoc::parse_demo_scenario("CLOSED_LOOP") == DemoScenario::ClosedLoop);

    CHECK(!fsoc::parse_demo_scenario("banana").has_value());
    CHECK(!fsoc::parse_demo_scenario("").has_value());
    CHECK(!fsoc::parse_demo_scenario("Static").has_value());        // case-sensitive
    CHECK(!fsoc::parse_demo_scenario("static ").has_value());       // exact match only

    CHECK(fsoc::demo_scenario_token(DemoScenario::OpenLoop) == "open");
    CHECK(fsoc::to_string(DemoScenario::OpenLoop) == "OPEN_LOOP");
    CHECK(fsoc::all_demo_scenarios().size() == 5u);

    const std::string help = fsoc::demo_help_text();
    CHECK(help.find("sinusoidal") != std::string::npos);
    CHECK(help.find("v1_baseline") != std::string::npos);
    CHECK(help.find("radians") != std::string::npos);
}

// ===================================================================
// 21. baseline PID gains untouched by the demo layer
// ===================================================================

void test_baseline_pid_unchanged() {
    const fsoc::SimulationRunnerConfig base = fsoc::baseline_runner_config();
    CHECK(base.controller.pan.kp == 12.0);
    CHECK(base.controller.pan.ki == 0.0);
    CHECK(base.controller.pan.kd == 0.0);
    CHECK(base.controller.tilt.kp == 12.0);
    CHECK(base.controller.tilt.ki == 0.0);
    CHECK(base.controller.tilt.kd == 0.0);

    for (const DemoScenario scenario : fsoc::all_demo_scenarios()) {
        const DemoScenarioPlan plan = fsoc::make_demo_scenario_plan(scenario);
        CHECK(plan.runner_config.controller.pan.kp == 12.0);
        CHECK(plan.runner_config.controller.pan.ki == 0.0);
        CHECK(plan.runner_config.controller.pan.kd == 0.0);
        CHECK(plan.runner_config.controller.tilt.kp == 12.0);
        CHECK(plan.runner_config.controller.tilt.ki == 0.0);
        CHECK(plan.runner_config.controller.tilt.kd == 0.0);
        CHECK(plan.runner_config.timestep_s == 0.02);
    }
}

// ===================================================================
// 22. Step-10 baseline acceptance still PASSES
// ===================================================================

void test_step10_acceptance_still_pass() {
    fsoc::ValidationSuite suite{{}};   // no filesystem output
    const fsoc::ValidationSuiteResult result = suite.run_all();
    CHECK(result.scenarios.size() == 7u);
    CHECK(result.overall_passed);
    for (const fsoc::ValidationResult& s : result.scenarios) {
        CHECK(s.passed);
    }
}

// ===================================================================
// 23. Steps 1-10 spot regression (build/ctest cover the rest)
// ===================================================================

void test_prior_steps_regression() {
    const fsoc::PanTiltCamera camera{fsoc::CameraConfig{}};
    CHECK(camera.project({100.0, 0.0, 0.0}).has_value());

    const fsoc::LinearTrajectory lin{{100.0, 0.0, 2.0}, {0.0, 1.0, 0.2}};
    CHECK(std::abs(lin.state_at(2.0).position_m.z - 2.4) < 1e-12);

    const fsoc::StationaryTrajectory st{fsoc::Vec3{100.0, 6.0, 4.0}};
    fsoc::SimulationRunner runner{fsoc::baseline_runner_config(), st};
    const fsoc::RecordedRun run = fsoc::run_and_record(runner, 3.0);
    const fsoc::BenchmarkMetrics bm =
        fsoc::compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);
    CHECK(bm.frames == 150u);
    CHECK(fsoc::rad_to_deg(bm.final_angular_error_rad) < 0.05);
}

// ===================================================================
// PAUSE + DemoRunState transitions (application state, not tracking)
// ===================================================================

void test_pause_and_run_state() {
    DemoSession session{DemoScenario::StaticAcquisition};
    CHECK(session.run_state() == DemoRunState::Ready);

    (void)session.step();
    CHECK(session.run_state() == DemoRunState::Running);

    const std::size_t frame_before = session.frame_index();
    const double time_before = session.simulation_time_s();

    session.pause();
    CHECK(session.run_state() == DemoRunState::Paused);
    const DemoSnapshot paused_a = session.step();
    const DemoSnapshot paused_b = session.step();
    CHECK(session.frame_index() == frame_before);          // NO physics advance
    CHECK(session.simulation_time_s() == time_before);
    CHECK(snapshots_equal(paused_a, paused_b));

    session.resume();
    CHECK(session.run_state() == DemoRunState::Running);
    (void)session.step();
    CHECK(session.frame_index() == frame_before + 1);      // advances again

    while (!session.finished()) {
        (void)session.step();
    }
    CHECK(session.run_state() == DemoRunState::Finished);
    const double time_finished = session.simulation_time_s();
    (void)session.step();                                  // no-op while Finished
    CHECK(session.simulation_time_s() == time_finished);
}

// ===================================================================
// NON-INTERFERENCE: DemoSession reproduces a bare SimulationRunner
// ===================================================================

void test_non_interference() {
    for (const DemoScenario scenario : fsoc::all_demo_scenarios()) {
        DemoScenarioPlan plan = fsoc::make_demo_scenario_plan(scenario);
        fsoc::SimulationRunner bare{plan.runner_config, *plan.trajectory};
        const std::size_t frames = plan.total_frames();
        std::vector<fsoc::SimulationStepResult> bare_steps;
        bare_steps.reserve(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            bare_steps.push_back(bare.step());
        }

        const SessionRun demo = run_session(scenario);
        CHECK(demo.steps.size() == bare_steps.size());
        bool identical = demo.steps.size() == bare_steps.size();
        for (std::size_t i = 0; i < bare_steps.size() && identical; ++i) {
            identical = step_results_equal(bare_steps[i], demo.steps[i]);
        }
        CHECK(identical);
    }
}

// ---- Stage-3 perception-mode-aware DemoSession constructor (additive) ----

// PerceptionMode::Classical through the new 4-arg constructor must be
// bit-identical to the existing 2-arg constructor -- the additive overload
// changes nothing about the frozen default demo path.
void test_perception_aware_constructor_classical_matches_default() {
    for (const DemoScenario scenario : fsoc::all_demo_scenarios()) {
        const double duration = 1.0;  // short, deterministic
        DemoSession baseline{scenario, duration};
        DemoSession explicit_classical{scenario, duration, fsoc::PerceptionMode::Classical};

        bool identical = true;
        while (!baseline.finished() && !explicit_classical.finished() && identical) {
            (void)baseline.step();
            (void)explicit_classical.step();
            identical = step_results_equal(baseline.last_step_result(), explicit_classical.last_step_result());
        }
        CHECK(identical);
        CHECK(baseline.finished() == explicit_classical.finished());
    }
}

// PerceptionMode::Hybrid must be REQUIRED to supply an ai_detector config
// (SimulationRunnerConfig::validate() enforces this, Stage 3) -- the demo
// constructor does not weaken that.
void test_perception_aware_constructor_requires_ai_detector_for_hybrid() {
    bool threw = false;
    try {
        DemoSession session{DemoScenario::StaticAcquisition, 1.0, fsoc::PerceptionMode::Hybrid};
        (void)session;
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {
    }
    CHECK(threw);
}

// Hybrid mode, given a valid model config, runs end-to-end and produces real
// (not default-constructed) perception telemetry.
void test_perception_aware_constructor_hybrid_runs_end_to_end() {
    fsoc::AiBeaconDetectorConfig ai_config{};
    ai_config.model_path = std::string(FSOC_PROJECT_SOURCE_DIR) + "/models/tiny_beacon_net.onnx";
    ai_config.presence_threshold = 0.95;

    DemoSession session{DemoScenario::StaticAcquisition, 1.0, fsoc::PerceptionMode::Hybrid, ai_config};
    bool saw_hybrid_mode = false;
    bool saw_any_ai_candidate = false;
    while (!session.finished()) {
        (void)session.step();
        const fsoc::TelemetryRecord& t = session.last_telemetry();
        if (t.perception_mode == "HYBRID") {
            saw_hybrid_mode = true;
        }
        if (t.ai_candidate_detected) {
            saw_any_ai_candidate = true;
            CHECK(t.ai_presence_probability.has_value());
            CHECK(*t.ai_presence_probability >= 0.95);
            CHECK(t.ai_inference_ms.has_value());
        }
    }
    CHECK(saw_hybrid_mode);
    CHECK(saw_any_ai_candidate);  // this scenario's beacon is clean/bright -> AI should fire at least once
}

}  // namespace

int main() {
    test_snapshot_copies_target();
    test_snapshot_camera();
    test_detection_present_when_tracking();
    test_snapshot_absent_when_lost();
    test_tracking_state_mapping();
    test_snapshot_command_and_saturation();
    test_core_is_radians();
    test_to_degrees_helper();
    test_session_static_starts_offaxis();
    test_session_static_converges();
    test_session_sinusoidal_in_validated_ranges();
    test_session_loss_and_reacquire();
    test_open_controller_disabled();
    test_closed_controller_enabled();
    test_open_closed_same_trajectory();
    test_reset_replay();
    test_two_sessions_identical();
    test_parse_scenario();
    test_baseline_pid_unchanged();
    test_step10_acceptance_still_pass();
    test_prior_steps_regression();
    test_pause_and_run_state();
    test_non_interference();
    test_perception_aware_constructor_classical_matches_default();
    test_perception_aware_constructor_requires_ai_detector_for_hybrid();
    test_perception_aware_constructor_hybrid_runs_end_to_end();

    if (failures == 0) {
        std::cout << "PASS: 26 Step-11 demo-packaging checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
