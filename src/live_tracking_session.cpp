#include "fsoc/live_tracking_session.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace fsoc {

void LiveTrackingSessionConfig::validate() const {
    calibration.validate();
    detector.validate();
    controller.validate();
    // LivePreprocessConfig has no additional invariants beyond its field types.
    actuator.validate();

    if (perception_mode != PerceptionMode::Classical) {
        if (!ai_detector.has_value()) {
            throw std::invalid_argument(
                "LiveTrackingSessionConfig: ai_detector config is required when "
                "perception_mode != Classical.");
        }
        ai_detector->validate();
    }
    if (tracker_enabled) {
        tracker.validate();
        if (!std::isfinite(tracker_min_confidence_to_steer) || tracker_min_confidence_to_steer < 0.0 ||
            tracker_min_confidence_to_steer > 1.0) {
            throw std::invalid_argument(
                "LiveTrackingSessionConfig: tracker_min_confidence_to_steer must be finite in [0, 1].");
        }
    }
}

namespace {
// The sensing-reference camera's cx/cy/fx/fy must be consistent with the
// PIXEL SPACE the detector actually measures in -- the preprocessed frame
// (LivePreprocessConfig target size), NOT the phone's raw capture
// resolution. Field of view is resolution-independent (the same physical
// light cone), so hfov/vfov carry over unchanged from the calibration; width/
// height do NOT (PanTiltCamera::fx_px() = (width_px/2) / tan(hfov/2) scales
// with width_px, so reusing the raw resolution here would silently scale
// every angular error by the raw/preprocessed size ratio).
CameraConfig sensing_camera_config(
    const LiveCameraCalibrationConfig& calibration, const LivePreprocessConfig& preprocess) {
    CameraConfig cfg{};
    cfg.width_px = preprocess.target_width_px;
    cfg.height_px = preprocess.target_height_px;
    cfg.hfov_rad = deg_to_rad(calibration.hfov_deg);
    cfg.vfov_rad = deg_to_rad(calibration.vfov_deg);
    cfg.validate();
    return cfg;
}
}  // namespace

LiveTrackingSession::LiveTrackingSession(LiveTrackingSessionConfig config)
    : config_((config.validate(), std::move(config))),
      detector_(config_.detector),
      controller_(config_.controller),
      actuator_(config_.actuator),
      sensing_camera_(sensing_camera_config(config_.calibration, config_.preprocess)) {
    if (config_.perception_mode != PerceptionMode::Classical) {
        ai_detector_.emplace(*config_.ai_detector);
    }
    if (config_.tracker_enabled) {
        tracker_.emplace(config_.tracker);
    }
}

LiveFrameResult LiveTrackingSession::process_frame(
    const Frame& raw_frame, const FrameSourceInfo& source_info, double dt_s) {
    if (!std::isfinite(dt_s) || dt_s <= 0.0) {
        throw std::invalid_argument("LiveTrackingSession::process_frame: dt_s must be finite and > 0.");
    }
    if (raw_frame.image.empty()) {
        throw std::invalid_argument("LiveTrackingSession::process_frame: raw_frame.image is empty.");
    }

    LiveFrameResult result{};
    result.frame_index = raw_frame.frame_index;
    result.timestamp_s = raw_frame.timestamp_s;
    result.dt_s = dt_s;
    result.source = source_info;
    result.raw_width_px = raw_frame.image.cols;
    result.raw_height_px = raw_frame.image.rows;

    // 1. preprocess — real frame -> the fixed-size CV_8UC1 both detectors require.
    const cv::Mat frame = preprocess_live_frame(raw_frame.image, config_.preprocess);
    result.preprocessed_width_px = frame.cols;
    result.preprocessed_height_px = frame.rows;

    // 2. detect — MEASUREMENT path, pixels only. Same call shape as
    // SimulationRunner::step(): Classical mode never constructs/runs the AI
    // detector.
    std::optional<BeaconDetection> classical_detection{};
    std::optional<AiBeaconDetection> ai_detection{};
    if (config_.perception_mode != PerceptionMode::AI) {
        classical_detection = detector_.detect(frame);
    }
    if (config_.perception_mode != PerceptionMode::Classical) {
        ai_detection = ai_detector_->detect(frame);
    }

    const PerceptionResult perception =
        resolve_perception(config_.perception_mode, classical_detection, ai_detection);
    result.detection = perception.detection;
    result.target_detected = perception.detection.has_value();
    result.perception = perception.diagnostics;

    // 3. state estimation / temporal gate (P0-v2, reused verbatim).
    if (config_.tracker_enabled) {
        const std::optional<ImagePoint> measurement =
            perception.detection.has_value()
                ? std::optional<ImagePoint>(ImagePoint{perception.detection->centroid_px.x_px,
                                                        perception.detection->centroid_px.y_px})
                : std::nullopt;
        result.tracked_state = tracker_->update(measurement, dt_s);
        if (is_safe_to_steer(result.tracked_state, config_.tracker_min_confidence_to_steer)) {
            result.detection =
                BeaconDetection{.centroid_px = {result.tracked_state.x_px, result.tracked_state.y_px}};
        } else {
            result.detection = std::nullopt;
        }
        result.target_detected = result.detection.has_value();
    }

    // 4. tracking error against the fixed sensing-reference camera (never
    // stepped — see class comment in the header).
    const std::optional<TrackingError> tracking_error =
        compute_tracking_error(result.detection, sensing_camera_);
    result.tracking_error = tracking_error;

    // 5. control, or the target-loss / open-loop policy (mirrors
    // SimulationRunner::step() exactly).
    ControlCommand command = zero_control_command();
    if (!config_.control_enabled) {
        controller_.reset();
    } else if (tracking_error.has_value()) {
        command = controller_.update(*tracking_error, dt_s);
    } else {
        controller_.reset();
    }
    result.command = command;

    // 6. step the VIRTUAL actuator only — never the sensing camera, and never
    // anything physical.
    result.actuator_state = actuator_.step(command.pan_rate_rad_s, command.tilt_rate_rad_s, dt_s);

    return result;
}

void LiveTrackingSession::reset() {
    controller_.reset();
    if (tracker_.has_value()) {
        tracker_->reset();
    }
    actuator_.reset();
}

}  // namespace fsoc
