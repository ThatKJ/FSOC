#pragma once

#include <cstddef>
#include <optional>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/camera.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/frame_source.hpp"
#include "fsoc/live_camera_calibration.hpp"
#include "fsoc/live_preprocessing.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/pid_controller.hpp"
#include "fsoc/target_tracker.hpp"
#include "fsoc/tracking_error.hpp"
#include "fsoc/virtual_actuator.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// LiveTrackingSession — the real-camera counterpart to SimulationRunner
// ---------------------------------------------------------------------------
//
// Mirrors SimulationRunner::step()'s control-path order EXACTLY:
//
//   preprocess -> classical (+ AI) detect -> resolve_perception
//              -> [tracker if enabled] -> compute_tracking_error
//              -> control (or loss/open-loop policy) -> virtual actuator step
//
// Differences from SimulationRunner, both deliberate:
//   * NO TargetState / CameraObservation / world truth exists or is computed
//     — there is nothing to compare a real detection against. LiveFrameResult
//     carries measurement and diagnostics only (Phase 7: never leak simulator
//     ground truth into real mode).
//   * The "camera" that provides cx/cy/fx/fy for compute_tracking_error is a
//     PanTiltCamera built from the calibration's declared/measured field of
//     view AT THE PREPROCESSED FRAME SIZE (not the phone's raw capture
//     resolution — fx_px/fy_px scale with resolution for a fixed FOV, and
//     the detector measures pixels in the preprocessed frame; see
//     live_tracking_session.cpp's sensing_camera_config()). It is NEVER
//     stepped — real-camera pixel error is always measured against a fixed
//     reference frame (image centre), because this milestone
//     does not physically move the sensing camera. The PID's output instead
//     drives a VirtualPanTiltActuator: honest bookkeeping only, see
//     fsoc/virtual_actuator.hpp.
//   * dt_s is the caller-measured wall-clock interval between frames (a real
//     camera has no fixed timestep), not a configured constant.

struct LiveTrackingSessionConfig {
    LiveCameraCalibrationConfig calibration{};
    BeaconDetectorConfig detector{};
    PIDControllerConfig controller{};
    LivePreprocessConfig preprocess{};
    VirtualActuatorConfig actuator{};

    // Perception seam (Stage 3, reused verbatim). Default Classical never
    // constructs/runs the AI detector.
    PerceptionMode perception_mode{PerceptionMode::Classical};
    std::optional<AiBeaconDetectorConfig> ai_detector{};

    // State-estimation / temporal-gate seam (P0-v2, reused verbatim).
    bool tracker_enabled{false};
    TargetTrackerConfig tracker{};
    double tracker_min_confidence_to_steer{0.4};

    // false -> PID is held reset every frame and the virtual actuator is
    // always commanded zero (observe-only mode; e.g. Manual Correction Assist
    // demos that only want the pointing-error readout, not a rate command).
    bool control_enabled{true};

    // Validates every sub-config (mirrors SimulationRunnerConfig::validate()'s
    // structure): calibration, detector, controller; ai_detector required iff
    // perception_mode != Classical; tracker + tracker_min_confidence_to_steer
    // iff tracker_enabled; actuator. Throws std::invalid_argument.
    void validate() const;
};

// One processed real-camera frame. NO target_truth, NO observation, NO
// target_visible field exists anywhere in this struct — there is no ground
// truth to report for a real camera. Every field here is either raw
// provenance (never fabricated) or derived from the same pixels-only
// pipeline the simulation uses.
struct LiveFrameResult {
    std::size_t frame_index{};
    double timestamp_s{};
    double dt_s{};

    // --- provenance (Phase 16: what Mission Control must label) ---
    FrameSourceInfo source{};  // .kind distinguishes REAL_CAMERA from SYNTHETIC
    int raw_width_px{};
    int raw_height_px{};
    int preprocessed_width_px{};
    int preprocessed_height_px{};

    // --- MEASUREMENT (the only path to a "detection" in this mode) ---
    std::optional<BeaconDetection> detection{};
    bool target_detected{};
    std::optional<TrackingError> tracking_error{};
    PerceptionDiagnostics perception{};
    TrackedState tracked_state{};  // default (Searching) when tracker_enabled == false

    // --- control (honest: does not move anything physical) ---
    ControlCommand command{};              // PID output, pre actuator clamp
    VirtualActuatorState actuator_state{};  // ACTUATOR_TYPE = VIRTUAL
};

class LiveTrackingSession {
public:
    explicit LiveTrackingSession(LiveTrackingSessionConfig config);

    [[nodiscard]] const LiveTrackingSessionConfig& config() const noexcept { return config_; }

    // Runs the full pipeline on one already-captured raw frame. `source_info`
    // is copied verbatim into the result (never re-derived: the caller's
    // FrameSource is the single source of truth for its own provenance).
    // Throws std::invalid_argument if dt_s is not finite and > 0, or if
    // raw_frame.image is empty.
    [[nodiscard]] LiveFrameResult process_frame(
        const Frame& raw_frame, const FrameSourceInfo& source_info, double dt_s);

    // Resets the PID, tracker (if enabled), and virtual actuator. Does not
    // touch the sensing-reference camera (it has no mutable tracking state).
    void reset();

private:
    LiveTrackingSessionConfig config_;
    BeaconDetector detector_;
    std::optional<AiBeaconDetector> ai_detector_;
    PIDController controller_;
    std::optional<TargetTracker> tracker_;
    VirtualPanTiltActuator actuator_;
    PanTiltCamera sensing_camera_;  // NEVER .step()-ed — see class comment above
};

}  // namespace fsoc
