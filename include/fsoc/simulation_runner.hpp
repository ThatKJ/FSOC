#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <opencv2/core.hpp>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/camera.hpp"
#include "fsoc/config.hpp"
#include "fsoc/demo_disturbance.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/observation.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/pid_controller.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/target_state.hpp"
#include "fsoc/target_tracker.hpp"
#include "fsoc/tracking_error.hpp"
#include "fsoc/trajectory.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// SimulationRunner  (Step 7 — the one intentional integration layer)
// ---------------------------------------------------------------------------
//
// Wires the already-tested modules into the deterministic fixed-step loop:
//
//   sim_time -> Trajectory -> observe_beacon -> SyntheticCameraRenderer(cv::Mat)
//            -> BeaconDetector -> compute_tracking_error -> PIDController
//            -> PanTiltCamera::step
//
// It owns ONLY: the clock, the fixed timestep, subsystem call order, the
// target-loss policy, and camera stepping. It owns NO domain mathematics and
// duplicates none (projection, trajectory, centroid, PID, pixel->angle all live
// in their modules).
//
// TRUTH vs MEASUREMENT (frozen): the control feedback path is exactly
//   detector.detect(frame) -> compute_tracking_error(detection, camera) -> pid.update
// TargetState / CameraObservation.image_point_px / the exact Projection are used
// ONLY for the diagnostic fields of SimulationStepResult (labelled "truth") and
// for scoring. None of them ever reaches the controller.

// One frame of the closed loop. Carries both truth and measurement because it is
// diagnostic output; the runner's control path never feeds a truth field to PID.
struct SimulationStepResult {
    double simulation_time_s{};
    std::size_t frame_index{};

    // --- TRUTH (diagnostics / scoring ONLY — never feeds control) ---
    TargetState target_truth{};
    CameraObservation observation{};                    // .image_point_px is the exact projection
    bool target_visible{};                              // observation.status == Visible

    // --- EXACT PERCEPTION INPUT (diagnostic/observer only) ---
    // The CV_8UC1 frame renderer_.render(observation) actually produced and
    // handed to detector_.detect() THIS step. Stored (not re-derived) so an
    // observer (TrackingVisualizer, evidence export) can annotate the exact
    // pixels the detector saw rather than re-rendering from `observation` —
    // re-rendering is only guaranteed identical while rendering is a pure,
    // noise-free function of the observation. cv::Mat is reference-counted, so
    // this assignment is a cheap header+refcount copy, not a pixel clone;
    // never read by the detector, tracking-error, PID, or camera themselves.
    cv::Mat rendered_frame{};

    // --- MEASUREMENT (the actual control feedback path) ---
    std::optional<BeaconDetection> detection{};         // from BeaconDetector, pixels only
    bool target_detected{};                             // detection.has_value()
    std::optional<TrackingError> tracking_error{};      // compute_tracking_error(detection, camera)
    ControlCommand command{};                           // PIDController output (pre camera clamp)
    AppliedRates applied_rates{};                       // what PanTiltCamera actually applied

    // --- camera orientation that produced THIS frame's observation (pre-step) ---
    double camera_pan_rad{};
    double camera_tilt_rad{};

    // --- diagnostic scoring: detected centroid vs exact projection (truth used here only) ---
    std::optional<double> detection_error_px{};

    // --- perception diagnostics (Stage 3, additive) — DIAGNOSTIC ONLY, never fed to control ---
    PerceptionDiagnostics perception{};

    // --- state estimation / temporal gate (P0-v2, additive) ---
    // Default-constructed (LockState::Searching) when config_.tracker_enabled
    // is false -- the tracker is never constructed and never runs in that
    // (default) case. When enabled, this is the tracker's own output state
    // for this frame; `detection` above already reflects whatever it decided
    // (see SimulationRunner::step() for the exact rule).
    TrackedState tracked_state{};
};

struct SimulationRunnerConfig {
    CameraConfig camera{};
    RendererConfig renderer{};                 // width/height must equal the camera's
    BeaconDetectorConfig detector{};
    PIDControllerConfig controller{};

    double timestep_s{0.02};                   // fixed step, 50 Hz; NOT wall-clock
    Vec3 camera_position_m{};                  // world frame; default = origin
    double initial_pan_rad{0.0};
    double initial_tilt_rad{0.0};

    // false -> open-loop: the camera is never steered and the PID is held at
    // reset every frame (used for the open- vs closed-loop comparison).
    bool control_enabled{true};

    // Perception seam (Stage 3). DEFAULT Classical -> bit-identical to v1
    // (regression-tested): the AI detector is never constructed and never
    // runs. `ai_detector` is REQUIRED (must have a value) iff perception_mode
    // != Classical; validate() enforces this.
    PerceptionMode perception_mode{PerceptionMode::Classical};
    std::optional<AiBeaconDetectorConfig> ai_detector{};

    // State-estimation / temporal-gate seam (P0-v2). DEFAULT false ->
    // bit-identical to pre-tracker behaviour (regression-tested): the tracker
    // is never constructed and never runs; `result.detection` is exactly
    // `resolve_perception()`'s own output, unchanged. When true, `tracker`
    // configures the alpha-beta filter + coast horizon + outlier gate, and
    // `tracker_min_confidence_to_steer` is the ONE threshold controlling
    // whether the controller may steer toward a coasted (predicted) position
    // during a brief dropout (fsoc::is_safe_to_steer) -- see
    // include/fsoc/target_tracker.hpp and docs/MVP_ABLATION.md.
    bool tracker_enabled{false};
    TargetTrackerConfig tracker{};
    double tracker_min_confidence_to_steer{0.4};

    // Demo disturbance seam (Phase J, additive). DEFAULT
    // DemoDisturbanceKind::None -> bit-identical to pre-disturbance behaviour
    // (regression-tested): apply_demo_disturbance() is never called, and
    // result.rendered_frame is exactly renderer_.render(observation),
    // unchanged. When non-None, the disturbance is applied to the rendered
    // frame BEFORE detection (both classical and AI see the disturbed
    // frame) and BEFORE it is stored in result.rendered_frame, using a seed
    // derived deterministically from frame_index -- see
    // include/fsoc/demo_disturbance.hpp and docs/MVP_ABLATION.md.
    DemoDisturbanceConfig disturbance{};

    // Validates every sub-config, that renderer dimensions match the camera, that
    // timestep_s is finite and > 0, that each PID output limit does not exceed
    // the corresponding camera actuator rate, that initial_pan_rad/initial_tilt_rad/
    // camera_position_m are all finite, that initial_tilt_rad lies within the
    // camera's [min_tilt_rad, max_tilt_rad] (otherwise PanTiltCamera would silently
    // clamp it instead of failing), that ai_detector is present whenever
    // perception_mode != Classical, that (iff tracker_enabled) `tracker` and
    // `tracker_min_confidence_to_steer` are themselves valid, and that
    // `disturbance` itself is valid. Throws std::invalid_argument.
    void validate() const;
};

// Empirically-tuned MVP baseline (NOT claimed optimal): P-dominant on the
// integrator plant, matched to the default CameraConfig actuator rate.
[[nodiscard]] SimulationRunnerConfig baseline_runner_config();

class SimulationRunner {
public:
    // The injected trajectory must outlive the runner (held by reference).
    SimulationRunner(SimulationRunnerConfig config, const Trajectory& trajectory);

    // Advance exactly one fixed timestep. Order: sample trajectory -> observe ->
    // render -> detect -> tracking error -> control (or loss policy) -> camera
    // step -> record -> advance time.
    [[nodiscard]] SimulationStepResult step();

    // Run ceil(duration_s / timestep_s) steps from the current time, collecting
    // results. Does NOT reset first.
    [[nodiscard]] std::vector<SimulationStepResult> run_for(double duration_s);

    // Back to t = 0, frame 0, camera at the configured initial pose, PID reset.
    void reset();

    [[nodiscard]] double simulation_time_s() const noexcept { return simulation_time_s_; }
    [[nodiscard]] std::size_t frame_index() const noexcept { return frame_index_; }
    [[nodiscard]] const PanTiltCamera& camera() const noexcept { return camera_; }
    [[nodiscard]] const SimulationRunnerConfig& config() const noexcept { return config_; }

private:
    SimulationRunnerConfig config_;
    const Trajectory* trajectory_;
    PanTiltCamera camera_;
    SyntheticCameraRenderer renderer_;
    BeaconDetector detector_;
    PIDController controller_;
    std::optional<AiBeaconDetector> ai_detector_;  // present iff perception_mode != Classical
    std::optional<TargetTracker> tracker_;         // present iff tracker_enabled
    double simulation_time_s_{0.0};
    std::size_t frame_index_{0};
};

// ---------------------------------------------------------------------------
// Simple closed-loop evaluation metrics (not the Step-8 telemetry system)
// ---------------------------------------------------------------------------
struct SimulationMetrics {
    std::size_t frame_count{};
    std::size_t detected_frames{};
    std::size_t visible_frames{};        // truth: target inside FOV
    std::size_t lost_frames{};           // frames with no detection
    double detection_fraction{};         // detected_frames / frame_count
    double rms_angular_error_rad{};      // over detected frames
    double max_angular_error_rad{};      // over detected frames
    double final_angular_error_rad{};    // last detected frame's total angular error
    double mean_detection_error_px{};    // truth-vs-measurement, diagnostic
};

// total angular error for one frame = hypot(pan_rad, tilt_rad).
[[nodiscard]] double total_angular_error_rad(const TrackingError& error) noexcept;

[[nodiscard]] SimulationMetrics evaluate(const std::vector<SimulationStepResult>& results);

}  // namespace fsoc
