#include "fsoc/demo.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "fsoc/config.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/target_state.hpp"
#include "fsoc/trajectory.hpp"

namespace fsoc {

// ===========================================================================
// Scenario parameter table  (reused verbatim from the Step-10 validation suite)
// ===========================================================================

namespace {

struct ScenarioSpec {
    enum class Kind { Stationary, Sinusoid };

    Kind kind{Kind::Stationary};
    Vec3 a{};   // Stationary: initial position.  Sinusoid: center_position_m
    Vec3 b{};   // Sinusoid: amplitude_m
    Vec3 c{};   // Sinusoid: frequency_hz
    double initial_tilt_rad{0.0};
    bool control_enabled{true};
    double duration_s{0.0};
};

// The numbers below are copied, unchanged, from src/validation.cpp
// (run_static_acquisition / run_sinusoidal / run_loss_and_reentry /
// run_open_vs_closed). Do NOT retune them — they are part of v1_baseline.
[[nodiscard]] ScenarioSpec spec_for(const DemoScenario scenario) {
    using K = ScenarioSpec::Kind;
    switch (scenario) {
        case DemoScenario::StaticAcquisition:
            return {K::Stationary, Vec3{100.0, 6.0, 4.0}, {}, {}, 0.0, true, 4.0};
        case DemoScenario::SinusoidalTracking:
            return {K::Sinusoid, Vec3{100.0, 0.0, 3.0}, Vec3{0.0, 22.0, 4.0},
                    Vec3{0.0, 0.12, 0.09}, std::atan2(3.0, 100.0), true, 20.0};
        case DemoScenario::LossReacquisition:
            return {K::Sinusoid, Vec3{100.0, 0.0, 3.0}, Vec3{0.0, 42.0, 4.0},
                    Vec3{0.0, 0.30, 0.10}, std::atan2(3.0, 100.0), true, 8.0};
        case DemoScenario::OpenLoop:
            return {K::Sinusoid, Vec3{100.0, 0.0, 3.0}, Vec3{0.0, 22.0, 4.0},
                    Vec3{0.0, 0.12, 0.09}, std::atan2(3.0, 100.0), false, 20.0};
        case DemoScenario::ClosedLoop:
            return {K::Sinusoid, Vec3{100.0, 0.0, 3.0}, Vec3{0.0, 22.0, 4.0},
                    Vec3{0.0, 0.12, 0.09}, std::atan2(3.0, 100.0), true, 20.0};
    }
    throw std::logic_error("spec_for: unhandled DemoScenario");
}

[[nodiscard]] std::unique_ptr<Trajectory> make_trajectory(const ScenarioSpec& spec) {
    if (spec.kind == ScenarioSpec::Kind::Stationary) {
        return std::make_unique<StationaryTrajectory>(spec.a);
    }
    SinusoidalTrajectory::Parameters params{};
    params.center_position_m = spec.a;
    params.amplitude_m = spec.b;
    params.frequency_hz = spec.c;
    return std::make_unique<SinusoidalTrajectory>(params);
}

// Baseline config, unchanged except for the scenario's initial tilt and the
// open/closed switch. The PID gains, timestep, camera, renderer, and detector
// all come straight from baseline_runner_config() — v1_baseline.
[[nodiscard]] SimulationRunnerConfig make_config(const ScenarioSpec& spec) {
    SimulationRunnerConfig config = baseline_runner_config();
    config.initial_tilt_rad = spec.initial_tilt_rad;
    config.control_enabled = spec.control_enabled;
    return config;
}

// Same scenario config, with the perception seam overridden (Stage 3). Used
// only by the additive PerceptionMode-aware DemoSession constructor below;
// the existing constructors never call this, so their behaviour (and every
// test built on them) is untouched.
[[nodiscard]] SimulationRunnerConfig make_config_with_perception(
    const ScenarioSpec& spec, const PerceptionMode mode,
    std::optional<AiBeaconDetectorConfig> ai_detector, const bool tracker_enabled) {
    SimulationRunnerConfig config = make_config(spec);
    config.perception_mode = mode;
    config.ai_detector = std::move(ai_detector);
    config.tracker_enabled = tracker_enabled;
    return config;
}

[[nodiscard]] std::size_t frame_count(const double duration_s, const double timestep_s) {
    return static_cast<std::size_t>(std::ceil(duration_s / timestep_s));
}

void require_positive_finite_duration(const double duration_s) {
    if (!std::isfinite(duration_s) || duration_s <= 0.0) {
        throw std::invalid_argument("DemoSession: duration_s must be finite and > 0.");
    }
}

[[nodiscard]] std::optional<double> opt_rad_to_deg(const std::optional<double>& value) {
    return value.has_value() ? std::optional<double>{rad_to_deg(*value)} : std::nullopt;
}

}  // namespace

// ===========================================================================
// Scenario names / parsing
// ===========================================================================

std::string_view to_string(const DemoScenario scenario) noexcept {
    switch (scenario) {
        case DemoScenario::StaticAcquisition:  return "STATIC_ACQUISITION";
        case DemoScenario::SinusoidalTracking: return "SINUSOIDAL_TRACKING";
        case DemoScenario::LossReacquisition:  return "TARGET_LOSS_REACQUISITION";
        case DemoScenario::OpenLoop:           return "OPEN_LOOP";
        case DemoScenario::ClosedLoop:         return "CLOSED_LOOP";
    }
    return "?";
}

std::string_view demo_scenario_token(const DemoScenario scenario) noexcept {
    switch (scenario) {
        case DemoScenario::StaticAcquisition:  return "static";
        case DemoScenario::SinusoidalTracking: return "sinusoidal";
        case DemoScenario::LossReacquisition:  return "loss";
        case DemoScenario::OpenLoop:           return "open";
        case DemoScenario::ClosedLoop:         return "closed";
    }
    return "?";
}

std::string_view demo_scenario_description(const DemoScenario scenario) noexcept {
    switch (scenario) {
        case DemoScenario::StaticAcquisition:
            return "initial coarse alignment onto a stationary terminal";
        case DemoScenario::SinusoidalTracking:
            return "closed-loop tracking of a nonlinear moving target (+/-12.4 deg sweep)";
        case DemoScenario::LossReacquisition:
            return "deliberate target loss and natural re-entry (baseline hold policy)";
        case DemoScenario::OpenLoop:
            return "controller disabled reference run (open loop)";
        case DemoScenario::ClosedLoop:
            return "same trajectory as 'open' with the controller enabled";
    }
    return "?";
}

double demo_scenario_duration_s(const DemoScenario scenario) noexcept {
    switch (scenario) {
        case DemoScenario::StaticAcquisition:  return 4.0;
        case DemoScenario::SinusoidalTracking: return 20.0;
        case DemoScenario::LossReacquisition:  return 8.0;
        case DemoScenario::OpenLoop:           return 20.0;
        case DemoScenario::ClosedLoop:         return 20.0;
    }
    return 0.0;
}

std::optional<DemoScenario> parse_demo_scenario(const std::string_view text) {
    for (const DemoScenario scenario : all_demo_scenarios()) {
        if (text == demo_scenario_token(scenario) || text == to_string(scenario)) {
            return scenario;
        }
    }
    return std::nullopt;
}

const std::vector<DemoScenario>& all_demo_scenarios() {
    static const std::vector<DemoScenario> kAll{
        DemoScenario::StaticAcquisition, DemoScenario::SinusoidalTracking,
        DemoScenario::LossReacquisition, DemoScenario::OpenLoop, DemoScenario::ClosedLoop};
    return kAll;
}

// ===========================================================================
// DemoScenarioPlan
// ===========================================================================

std::size_t DemoScenarioPlan::total_frames() const {
    return frame_count(duration_s, runner_config.timestep_s);
}

DemoScenarioPlan make_demo_scenario_plan(const DemoScenario scenario) {
    const ScenarioSpec spec = spec_for(scenario);
    DemoScenarioPlan plan{};
    plan.scenario = scenario;
    plan.runner_config = make_config(spec);
    plan.trajectory = make_trajectory(spec);
    plan.duration_s = spec.duration_s;
    return plan;
}

// ===========================================================================
// Tracking-state mirror
// ===========================================================================

std::string_view to_string(const DemoTrackingState state) noexcept {
    return state == DemoTrackingState::Tracking ? "TRACKING" : "TARGET_LOST";
}

DemoTrackingState to_demo_tracking_state(const TrackingState state) noexcept {
    return state == TrackingState::Tracking ? DemoTrackingState::Tracking
                                            : DemoTrackingState::TargetLost;
}

std::string_view to_string(const DemoRunState state) noexcept {
    switch (state) {
        case DemoRunState::Ready:    return "READY";
        case DemoRunState::Running:  return "RUNNING";
        case DemoRunState::Paused:   return "PAUSED";
        case DemoRunState::Finished: return "FINISHED";
    }
    return "?";
}

// ===========================================================================
// DemoSnapshot conversion  (pure observer — copies only)
// ===========================================================================

DemoSnapshot make_demo_snapshot(
    const SimulationStepResult& result,
    const TelemetryRecord& telemetry,
    const CameraConfig& camera) {
    DemoSnapshot snapshot{};

    // Clock / frame index: authoritative from the step result.
    snapshot.simulation_time_s = result.simulation_time_s;
    snapshot.frame_index = result.frame_index;
    snapshot.state = to_demo_tracking_state(telemetry.tracking_state);

    // Target truth (diagnostic display only — never a control input).
    snapshot.target.x_m = result.target_truth.position_m.x;
    snapshot.target.y_m = result.target_truth.position_m.y;
    snapshot.target.z_m = result.target_truth.position_m.z;
    snapshot.target.vx_mps = result.target_truth.velocity_mps.x;
    snapshot.target.vy_mps = result.target_truth.velocity_mps.y;
    snapshot.target.vz_mps = result.target_truth.velocity_mps.z;

    // Camera pose that produced this frame's observation, plus the rate the
    // actuator actually applied and the fixed FOV.
    snapshot.camera.pan_rad = result.camera_pan_rad;
    snapshot.camera.tilt_rad = result.camera_tilt_rad;
    snapshot.camera.pan_rate_rad_s = result.applied_rates.pan_rate_rad_s;
    snapshot.camera.tilt_rate_rad_s = result.applied_rates.tilt_rate_rad_s;
    snapshot.camera.horizontal_fov_rad = camera.hfov_rad;
    snapshot.camera.vertical_fov_rad = camera.vfov_rad;

    // Measurement view — taken from the flat TelemetryRecord (the Step-8
    // canonical measurement projection); std::nullopt when unavailable.
    snapshot.detection.detected = result.target_detected;
    snapshot.detection.x_px = telemetry.detected_x_px;
    snapshot.detection.y_px = telemetry.detected_y_px;

    snapshot.tracking.error_x_px = telemetry.pixel_error_x_px;
    snapshot.tracking.error_y_px = telemetry.pixel_error_y_px;
    snapshot.tracking.pan_error_rad = telemetry.angular_error_pan_rad;
    snapshot.tracking.tilt_error_rad = telemetry.angular_error_tilt_rad;
    snapshot.tracking.total_error_rad = telemetry.angular_error_total_rad;

    // Controller output (pre actuator clamp) + the flat-out-slew flags.
    snapshot.control.command_pan_rate_rad_s = result.command.pan_rate_rad_s;
    snapshot.control.command_tilt_rate_rad_s = result.command.tilt_rate_rad_s;
    snapshot.control.pan_saturated = telemetry.pan_saturated;
    snapshot.control.tilt_saturated = telemetry.tilt_saturated;

    return snapshot;
}

DemoSnapshotAnglesDeg to_degrees(const DemoSnapshot& snapshot) {
    DemoSnapshotAnglesDeg out{};
    out.camera_pan_deg = rad_to_deg(snapshot.camera.pan_rad);
    out.camera_tilt_deg = rad_to_deg(snapshot.camera.tilt_rad);
    out.camera_pan_rate_deg_s = rad_to_deg(snapshot.camera.pan_rate_rad_s);
    out.camera_tilt_rate_deg_s = rad_to_deg(snapshot.camera.tilt_rate_rad_s);
    out.horizontal_fov_deg = rad_to_deg(snapshot.camera.horizontal_fov_rad);
    out.vertical_fov_deg = rad_to_deg(snapshot.camera.vertical_fov_rad);
    out.pan_error_deg = opt_rad_to_deg(snapshot.tracking.pan_error_rad);
    out.tilt_error_deg = opt_rad_to_deg(snapshot.tracking.tilt_error_rad);
    out.total_error_deg = opt_rad_to_deg(snapshot.tracking.total_error_rad);
    out.command_pan_rate_deg_s = rad_to_deg(snapshot.control.command_pan_rate_rad_s);
    out.command_tilt_rate_deg_s = rad_to_deg(snapshot.control.command_tilt_rate_rad_s);
    return out;
}

// ===========================================================================
// DemoSession
// ===========================================================================

DemoSession::DemoSession(const DemoScenario scenario)
    : DemoSession(scenario, demo_scenario_duration_s(scenario)) {}

DemoSession::DemoSession(const DemoScenario scenario, const double duration_s)
    : scenario_(scenario),
      trajectory_(make_trajectory(spec_for(scenario))),
      config_(make_config(spec_for(scenario))),
      duration_s_((require_positive_finite_duration(duration_s), duration_s)),
      total_frames_(frame_count(duration_s_, config_.timestep_s)),
      max_pan_rate_rad_s_(config_.camera.max_pan_rate_rad_s),
      max_tilt_rate_rad_s_(config_.camera.max_tilt_rate_rad_s),
      runner_(config_, *trajectory_) {}

// Additive (Stage 3): identical to the two-arg constructor except the
// perception seam is overridden. Passing PerceptionMode::Classical here is
// bit-identical to the constructor above (both end up with the same
// baseline_runner_config()-derived config); the two-arg constructor is
// otherwise untouched and remains the one every pre-Stage-3 caller/test uses.
DemoSession::DemoSession(
    const DemoScenario scenario, const double duration_s, const PerceptionMode mode,
    std::optional<AiBeaconDetectorConfig> ai_detector, const bool tracker_enabled)
    : scenario_(scenario),
      trajectory_(make_trajectory(spec_for(scenario))),
      config_(make_config_with_perception(spec_for(scenario), mode, std::move(ai_detector), tracker_enabled)),
      duration_s_((require_positive_finite_duration(duration_s), duration_s)),
      total_frames_(frame_count(duration_s_, config_.timestep_s)),
      max_pan_rate_rad_s_(config_.camera.max_pan_rate_rad_s),
      max_tilt_rate_rad_s_(config_.camera.max_tilt_rate_rad_s),
      runner_(config_, *trajectory_) {}

DemoSnapshot DemoSession::step() {
    if (run_state_ == DemoRunState::Ready) {
        run_state_ = DemoRunState::Running;
    }
    if (run_state_ != DemoRunState::Running) {
        // Paused or Finished: no physics advance, no hidden simulation.
        return last_snapshot_;
    }

    last_step_result_ = runner_.step();
    last_telemetry_ =
        make_telemetry_record(last_step_result_, max_pan_rate_rad_s_, max_tilt_rate_rad_s_);
    last_snapshot_ = make_demo_snapshot(last_step_result_, last_telemetry_, config_.camera);

    if (runner_.frame_index() >= total_frames_) {
        run_state_ = DemoRunState::Finished;
    }
    return last_snapshot_;
}

void DemoSession::reset() {
    runner_.reset();
    run_state_ = DemoRunState::Ready;
    last_snapshot_ = DemoSnapshot{};
    last_telemetry_ = TelemetryRecord{};
    last_step_result_ = SimulationStepResult{};
}

void DemoSession::pause() noexcept {
    if (run_state_ == DemoRunState::Running || run_state_ == DemoRunState::Ready) {
        run_state_ = DemoRunState::Paused;
    }
}

void DemoSession::resume() noexcept {
    if (run_state_ == DemoRunState::Paused) {
        run_state_ = DemoRunState::Running;
    }
}

bool DemoSession::finished() const noexcept {
    return runner_.frame_index() >= total_frames_;
}

double DemoSession::simulation_time_s() const noexcept {
    return runner_.simulation_time_s();
}

std::size_t DemoSession::frame_index() const noexcept {
    return runner_.frame_index();
}

const SimulationRunnerConfig& DemoSession::runner_config() const noexcept {
    return runner_.config();
}

std::vector<DemoSnapshot> DemoSession::run_to_completion() {
    if (run_state_ == DemoRunState::Paused) {
        run_state_ = DemoRunState::Running;  // completion implies running
    }
    std::vector<DemoSnapshot> snapshots;
    const std::size_t done = std::min(frame_index(), total_frames_);
    snapshots.reserve(total_frames_ - done);
    while (!finished()) {
        snapshots.push_back(step());
    }
    return snapshots;
}

// ===========================================================================
// Help text
// ===========================================================================

std::string demo_help_text() {
    std::string help;
    help += "fsoc_demo - SIH26169 FSOC baseline demo runner (v1_baseline, FROZEN)\n\n";
    help += "Usage:\n";
    help += "  fsoc_demo <scenario> [--mode classical|ai|hybrid] [--tracker] [--duration <seconds>]\n";
    help += "            [--csv <path>] [--quiet]\n";
    help += "  fsoc_demo --help\n\n";
    help += "Scenarios:\n";
    for (const DemoScenario scenario : all_demo_scenarios()) {
        const std::string_view token = demo_scenario_token(scenario);
        help += "  ";
        help += token;
        for (std::size_t pad = token.size(); pad < 12; ++pad) {
            help += ' ';
        }
        help += demo_scenario_description(scenario);
        help += '\n';
    }
    help += "\nOptions:\n";
    help += "  --mode <mode>         classical (default, v1_baseline) | ai | hybrid\n";
    help += "                        ai/hybrid run the Stage-3 C++ ONNX TinyBeaconNet\n";
    help += "                        detector (models/tiny_beacon_net.onnx); falls back to\n";
    help += "                        classical with a warning if the model can't be loaded\n";
    help += "  --tracker             enable the alpha-beta state estimator + temporal-\n";
    help += "                        consistency gate (default off); bridges brief detection\n";
    help += "                        gaps and rejects implausible jumps -- see\n";
    help += "                        include/fsoc/target_tracker.hpp and docs/MVP_ABLATION.md\n";
    help += "  --duration <seconds>  override the scenario's validated duration (demo knob only)\n";
    help += "  --csv <path>          write the telemetry CSV for this run\n";
    help += "                        (27 Step-8 fields + 7 Stage-3 perception fields\n";
    help += "                        + 8 P0-v2 tracker fields, docs/08_TELEMETRY_SCHEMA.md)\n";
    help += "  --quiet               print only the end-of-run summary\n\n";
    help += "Notes:\n";
    help += "  Fixed 50 Hz simulation (dt = 0.02 s). The baseline PID (kp=12, ki=0, kd=0),\n";
    help += "  the classical detector, and v1_baseline itself are frozen and UNCHANGED by\n";
    help += "  --mode or --tracker; they only change which perception/estimation path feeds\n";
    help += "  the SAME control loop (ADR-018 Safe Hybrid: AI never gets independent control\n";
    help += "  authority; ADR-019: the tracker is a post-perception gate, not a fusion change).\n";
    help += "  Core values are radians; the CLI prints degrees.";
    return help;
}

}  // namespace fsoc
