#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fsoc/config.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/telemetry.hpp"
#include "fsoc/trajectory.hpp"

namespace fsoc {

// ===========================================================================
// Demo packaging layer  (Step 11 — additive, presentation-facing)
// ===========================================================================
//
// This header packages the FROZEN v1_baseline engine for the polished SIH demo
// and a future web frontend. It adds NOTHING to the closed loop: every value
// here is copied out of a SimulationStepResult / TelemetryRecord that the
// existing Step 7/8 code already produced. No physics, no control law, no
// geometry, no estimation, no networking, no JSON.
//
//   C++ engine  ->  DemoSnapshot (this file)  ->  [future transport / WebSocket]
//               ->  Next.js / Three.js mission-control frontend
//
// Units: the C++ core is RADIANS throughout. Degrees appear only at the UI
// boundary (to_degrees() below, and the documented JSON in
// docs/18_FRONTEND_DATA_CONTRACT.md). The two are never mixed.
//
// State: TrackingState (TRACKING / TARGET_LOST) is control/observation state.
// DemoRunState (Ready / Running / Paused / Finished) is application state. They
// are kept conceptually separate — PAUSED is not a kind of TARGET_LOST.

// ---------------------------------------------------------------------------
// Demo scenario presets
// ---------------------------------------------------------------------------
//
// Presentation-facing selection only. These REUSE the validated Step-10
// trajectory/config parameters verbatim (see docs/16_BASELINE_ACCEPTANCE.md);
// nothing is retuned. DemoScenario is NOT a replacement for ValidationScenarioId.
enum class DemoScenario {
    StaticAcquisition,   // Step-10 "Static Acquisition"       — stationary {100,6,4} m, 4 s
    SinusoidalTracking,  // Step-10 "Sinusoidal Tracking"      — +/-12.4 deg sweep, 20 s
    LossReacquisition,   // Step-10 "Target Loss and Re-entry" — aggressive sinusoid, 8 s
    OpenLoop,            // Step-10 "Open vs Closed Loop", control_enabled = false, 20 s
    ClosedLoop,          // same trajectory as OpenLoop, control_enabled = true,   20 s
};

// Canonical SCREAMING_SNAKE name, e.g. "STATIC_ACQUISITION". Stable identifier
// for logs / UI / the JSON contract.
[[nodiscard]] std::string_view to_string(DemoScenario scenario) noexcept;

// Short CLI token: one of "static", "sinusoidal", "loss", "open", "closed".
[[nodiscard]] std::string_view demo_scenario_token(DemoScenario scenario) noexcept;

// One-line human description used by `fsoc_demo --help`.
[[nodiscard]] std::string_view demo_scenario_description(DemoScenario scenario) noexcept;

// The validated run duration for the scenario (seconds); matches Step 10.
[[nodiscard]] double demo_scenario_duration_s(DemoScenario scenario) noexcept;

// Accepts either the CLI token ("static") or the canonical name
// ("STATIC_ACQUISITION"), case-sensitive. Returns std::nullopt for anything
// else so the CLI turns bad input into a clean usage error, never a crash.
[[nodiscard]] std::optional<DemoScenario> parse_demo_scenario(std::string_view text);

// All five presets in enum order (for --help and tests).
[[nodiscard]] const std::vector<DemoScenario>& all_demo_scenarios();

// ---------------------------------------------------------------------------
// Demo disturbance presets  (MVP-V2 Phase J — additive, presentation-facing)
// ---------------------------------------------------------------------------
//
// Deterministic, NAMED one-command demo conditions -- NOT a UI editor and not
// a new degree of freedom for a caller to hand-tune. Each preset composes
// EXISTING, already-validated knobs (a DemoScenario base trajectory,
// PerceptionMode, tracker_enabled, DemoDisturbanceConfig) into one ready-to-
// run bundle. Every preset is grounded in a specific measured finding from
// docs/MVP_ABLATION.md (Phase E/F/G) -- see
// demo_disturbance_scenario_description() for the mapping, and DECISIONS.md
// ADR-020 for why each preset picks the perception/tracker config it does.
enum class DemoDisturbanceScenario {
    Normal,         // Classical, tracker off, no disturbance -- the calm baseline
    Noise,          // Classical, tracker off, additive read noise (Stage-4 LowSnr: unaided Classical already copes)
    Occlusion,      // Hybrid+Tracker, a short (2-frame) occlusion window -- bridged (Phase G DROPOUT_2_FRAME)
    Clutter,        // Hybrid+Tracker, a bright random distractor every frame -- false-lock mitigation (Phase E/F)
    Reacquisition,  // Hybrid+Tracker, a long occlusion window -- full Lost -> fresh reacquire (Phase G DROPOUT_LONGER)
};

[[nodiscard]] std::string_view to_string(DemoDisturbanceScenario scenario) noexcept;

// Short CLI token: one of "normal", "noise", "occlusion", "clutter", "reacquisition".
[[nodiscard]] std::string_view demo_disturbance_scenario_token(DemoDisturbanceScenario scenario) noexcept;

// One-line human description (what it demonstrates and which finding it's
// grounded in) used by `fsoc_demo --help`.
[[nodiscard]] std::string_view demo_disturbance_scenario_description(DemoDisturbanceScenario scenario) noexcept;

[[nodiscard]] std::optional<DemoDisturbanceScenario> parse_demo_disturbance_scenario(std::string_view text);

// All five presets in enum order (for --help and tests).
[[nodiscard]] const std::vector<DemoDisturbanceScenario>& all_demo_disturbance_scenarios();

// ---------------------------------------------------------------------------
// DemoScenarioPlan — what a scenario expands into
// ---------------------------------------------------------------------------
//
// The single place a DemoScenario becomes runnable pieces. The trajectory is
// heap-owned so a SimulationRunner (which holds the trajectory by reference)
// can be constructed after it at a stable address.
struct DemoScenarioPlan {
    DemoScenario scenario{};
    SimulationRunnerConfig runner_config{};
    std::unique_ptr<Trajectory> trajectory{};
    double duration_s{};

    // ceil(duration_s / runner_config.timestep_s) — the fixed-step frame count.
    [[nodiscard]] std::size_t total_frames() const;
};

[[nodiscard]] DemoScenarioPlan make_demo_scenario_plan(DemoScenario scenario);

// ---------------------------------------------------------------------------
// Presentation-facing tracking state
// ---------------------------------------------------------------------------
//
// A 1:1 mirror of fsoc::TrackingState, re-declared so the frontend contract
// does not depend on a telemetry header. NO extra / "AI" states.
enum class DemoTrackingState {
    Tracking,
    TargetLost,
};

[[nodiscard]] std::string_view to_string(DemoTrackingState state) noexcept;  // "TRACKING" / "TARGET_LOST"
[[nodiscard]] DemoTrackingState to_demo_tracking_state(TrackingState state) noexcept;

// ---------------------------------------------------------------------------
// DemoSnapshot — one simulation frame, as the UI needs it
// ---------------------------------------------------------------------------
//
// A pure COPY / VIEW MODEL, built from (SimulationStepResult + TelemetryRecord
// + the CameraConfig the runner used). It is never read by the control loop.
// Angles are RADIANS. Optionals are std::nullopt when the measurement does not
// exist this frame (target lost) — no sentinel values.
struct DemoSnapshot {
    double simulation_time_s{};
    std::size_t frame_index{};
    DemoTrackingState state{DemoTrackingState::TargetLost};

    struct Target {
        double x_m{};
        double y_m{};
        double z_m{};
        double vx_mps{};
        double vy_mps{};
        double vz_mps{};
    } target{};

    struct Camera {
        double pan_rad{};
        double tilt_rad{};
        double pan_rate_rad_s{};   // rate the actuator actually applied this frame
        double tilt_rate_rad_s{};
        double horizontal_fov_rad{};
        double vertical_fov_rad{};
    } camera{};

    struct Detection {
        bool detected{};
        std::optional<double> x_px{};
        std::optional<double> y_px{};
    } detection{};

    struct Tracking {
        std::optional<double> error_x_px{};
        std::optional<double> error_y_px{};
        std::optional<double> pan_error_rad{};
        std::optional<double> tilt_error_rad{};
        std::optional<double> total_error_rad{};
    } tracking{};

    struct Control {
        double command_pan_rate_rad_s{};   // PID output, pre actuator clamp
        double command_tilt_rate_rad_s{};
        bool pan_saturated{};
        bool tilt_saturated{};
    } control{};
};

// Pure observer conversion. No I/O, no reach-back into the loop, no mutation of
// its inputs. `camera` supplies only the FOV (and is the same CameraConfig the
// runner used).
[[nodiscard]] DemoSnapshot make_demo_snapshot(
    const SimulationStepResult& result,
    const TelemetryRecord& telemetry,
    const CameraConfig& camera);

// ---------------------------------------------------------------------------
// UI-boundary unit view (degrees)
// ---------------------------------------------------------------------------
//
// The only place degrees appear. Pixels and metres are unit-invariant and stay
// on DemoSnapshot; this carries just the angular quantities converted to degrees
// for a human / UI consumer. See docs/18_FRONTEND_DATA_CONTRACT.md.
struct DemoSnapshotAnglesDeg {
    double camera_pan_deg{};
    double camera_tilt_deg{};
    double camera_pan_rate_deg_s{};
    double camera_tilt_rate_deg_s{};
    double horizontal_fov_deg{};
    double vertical_fov_deg{};
    std::optional<double> pan_error_deg{};
    std::optional<double> tilt_error_deg{};
    std::optional<double> total_error_deg{};
    double command_pan_rate_deg_s{};
    double command_tilt_rate_deg_s{};
};

[[nodiscard]] DemoSnapshotAnglesDeg to_degrees(const DemoSnapshot& snapshot);

// ---------------------------------------------------------------------------
// DemoRunState — application / session state (NOT tracking state)
// ---------------------------------------------------------------------------
//
// Ready    -> constructed / just reset, no frame stepped yet
// Running  -> stepping the fixed-rate simulation
// Paused   -> step() returns the last frame and does NOT advance sim time
// Finished -> every frame for the scenario duration has been produced
enum class DemoRunState {
    Ready,
    Running,
    Paused,
    Finished,
};

[[nodiscard]] std::string_view to_string(DemoRunState state) noexcept;

// ---------------------------------------------------------------------------
// DemoSession — deterministic packaging of one scenario run
// ---------------------------------------------------------------------------
//
// Composition over the validated stack: owns a heap trajectory + a
// SimulationRunner + the telemetry conversion. It implements NO physics. Two
// sessions of the same scenario produce byte-identical snapshot sequences;
// reset() reproduces the exact same run.
class DemoSession {
public:
    explicit DemoSession(DemoScenario scenario);
    // Demo knob: override the scenario's validated duration (still fixed 50 Hz).
    // Throws std::invalid_argument if duration_s is not finite and > 0.
    DemoSession(DemoScenario scenario, double duration_s);
    // Additive (Stage 3): same as above, with the perception seam overridden.
    // `mode == PerceptionMode::Classical` is bit-identical to the constructor
    // above. `ai_detector` is required (throws std::invalid_argument via
    // SimulationRunnerConfig::validate()) whenever mode != Classical.
    // `tracker_enabled` (P0-v2, additive): default false is bit-identical to
    // this constructor's pre-tracker behaviour; true layers TargetTracker
    // (default TargetTrackerConfig, tracker_min_confidence_to_steer = 0.4)
    // after whichever perception mode is selected -- see
    // include/fsoc/target_tracker.hpp and docs/MVP_ABLATION.md.
    DemoSession(
        DemoScenario scenario, double duration_s, PerceptionMode mode,
        std::optional<AiBeaconDetectorConfig> ai_detector = std::nullopt, bool tracker_enabled = false);

    // Phase J: one of the five named, deterministic disturbance presets
    // (see DemoDisturbanceScenario above) -- composes an existing base
    // scenario + perception mode + tracker_enabled + disturbance config.
    // `ai_detector` is required for the presets that use Hybrid (Occlusion,
    // Clutter, Reacquisition); Normal/Noise use Classical and ignore it.
    explicit DemoSession(
        DemoDisturbanceScenario preset, std::optional<AiBeaconDetectorConfig> ai_detector = std::nullopt);

    DemoSession(const DemoSession&) = delete;
    DemoSession& operator=(const DemoSession&) = delete;
    DemoSession(DemoSession&&) = delete;
    DemoSession& operator=(DemoSession&&) = delete;

    // Advance exactly one fixed timestep and return that frame's snapshot.
    // While Paused or Finished this advances nothing and returns the last
    // snapshot (a default snapshot if no frame has been stepped yet).
    [[nodiscard]] DemoSnapshot step();

    // Back to t = 0, frame 0, camera at the initial pose, PID reset. The
    // selected scenario is preserved; the next run is bit-identical.
    void reset();

    // Running <-> Paused. No-ops from an incompatible state. Neither call ever
    // advances or rewinds simulation time.
    void pause() noexcept;
    void resume() noexcept;

    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] DemoScenario scenario() const noexcept { return scenario_; }
    [[nodiscard]] DemoRunState run_state() const noexcept { return run_state_; }
    [[nodiscard]] double simulation_time_s() const noexcept;
    [[nodiscard]] std::size_t frame_index() const noexcept;
    [[nodiscard]] double duration_s() const noexcept { return duration_s_; }
    [[nodiscard]] std::size_t total_frames() const noexcept { return total_frames_; }
    [[nodiscard]] const SimulationRunnerConfig& runner_config() const noexcept;

    // The three views of the most recently stepped frame. last_snapshot() is the
    // frontend contract; the other two are the unchanged Step 7/8 records that
    // the CLI's CSV + BenchmarkMetrics need and the non-interference test
    // compares against a bare SimulationRunner.
    [[nodiscard]] const DemoSnapshot& last_snapshot() const noexcept { return last_snapshot_; }
    [[nodiscard]] const TelemetryRecord& last_telemetry() const noexcept { return last_telemetry_; }
    [[nodiscard]] const SimulationStepResult& last_step_result() const noexcept {
        return last_step_result_;
    }

    // Step from the current state until finished(), collecting every snapshot.
    // Forces Running first (completion implies running).
    [[nodiscard]] std::vector<DemoSnapshot> run_to_completion();

private:
    DemoScenario scenario_;
    std::unique_ptr<Trajectory> trajectory_;   // constructed FIRST — outlives runner_
    SimulationRunnerConfig config_;
    double duration_s_;
    std::size_t total_frames_;
    double max_pan_rate_rad_s_;
    double max_tilt_rate_rad_s_;
    SimulationRunner runner_;                   // declared AFTER trajectory_ + config_
    DemoRunState run_state_{DemoRunState::Ready};
    DemoSnapshot last_snapshot_{};
    TelemetryRecord last_telemetry_{};
    SimulationStepResult last_step_result_{};
};

// Multi-line help text for `fsoc_demo --help` (tokens + descriptions + the
// units / frozen-baseline notes). No trailing newline.
[[nodiscard]] std::string demo_help_text();

}  // namespace fsoc
