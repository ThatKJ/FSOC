#include "fsoc/simulation_runner.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <opencv2/core.hpp>

namespace fsoc {

void SimulationRunnerConfig::validate() const {
    camera.validate();
    renderer.validate();
    detector.validate();
    controller.validate();

    if (renderer.width_px != camera.width_px || renderer.height_px != camera.height_px) {
        throw std::invalid_argument(
            "SimulationRunnerConfig: renderer dimensions must match the camera "
            "(build the renderer config with renderer_config_for(camera)).");
    }
    if (!std::isfinite(timestep_s) || timestep_s <= 0.0) {
        throw std::invalid_argument("SimulationRunnerConfig: timestep_s must be finite and > 0.");
    }
    // The PID must not silently demand a rate the actuator cannot deliver.
    if (controller.pan.output_limit_rad_s > camera.max_pan_rate_rad_s) {
        throw std::invalid_argument(
            "SimulationRunnerConfig: PID pan output limit exceeds camera max pan rate.");
    }
    if (controller.tilt.output_limit_rad_s > camera.max_tilt_rate_rad_s) {
        throw std::invalid_argument(
            "SimulationRunnerConfig: PID tilt output limit exceeds camera max tilt rate.");
    }
    // Initial pose / position sanity. Without these checks an out-of-range or
    // wrong-unit initial_tilt_rad (e.g. degrees typed where radians are
    // expected) is NOT rejected here -- it reaches PanTiltCamera's constructor,
    // which silently std::clamp()s tilt to [min_tilt_rad, max_tilt_rad] instead
    // of failing, so the scenario quietly starts at the wrong attitude. That is
    // this codebase's equivalent of the "misspelled parameter silently changes
    // behaviour" bug class: the field name is spelled correctly (the compiler
    // already guarantees that for a plain C++ struct) but an invalid *value*
    // is coerced rather than rejected. pan is intentionally NOT range-checked
    // beyond finiteness: PanTiltCamera::wrap_pi() treats pan as periodic by
    // design, so wrapping it is correct behaviour, not silent data loss.
    if (!std::isfinite(initial_pan_rad)) {
        throw std::invalid_argument("SimulationRunnerConfig: initial_pan_rad must be finite.");
    }
    if (!std::isfinite(initial_tilt_rad)) {
        throw std::invalid_argument("SimulationRunnerConfig: initial_tilt_rad must be finite.");
    }
    if (initial_tilt_rad < camera.min_tilt_rad || initial_tilt_rad > camera.max_tilt_rad) {
        throw std::invalid_argument(
            "SimulationRunnerConfig: initial_tilt_rad is outside the camera's "
            "[min_tilt_rad, max_tilt_rad] range (it would otherwise be silently clamped).");
    }
    if (!std::isfinite(camera_position_m.x) || !std::isfinite(camera_position_m.y) ||
        !std::isfinite(camera_position_m.z)) {
        throw std::invalid_argument("SimulationRunnerConfig: camera_position_m must be finite.");
    }
    if (perception_mode != PerceptionMode::Classical) {
        if (!ai_detector.has_value()) {
            throw std::invalid_argument(
                "SimulationRunnerConfig: ai_detector config is required when "
                "perception_mode != Classical.");
        }
        ai_detector->validate();
    }
    if (tracker_enabled) {
        tracker.validate();
        if (!std::isfinite(tracker_min_confidence_to_steer) || tracker_min_confidence_to_steer < 0.0 ||
            tracker_min_confidence_to_steer > 1.0) {
            throw std::invalid_argument(
                "SimulationRunnerConfig: tracker_min_confidence_to_steer must be finite in [0, 1].");
        }
    }
    disturbance.validate();
}

SimulationRunnerConfig baseline_runner_config() {
    SimulationRunnerConfig config{};
    config.camera = CameraConfig{};
    config.renderer = renderer_config_for(config.camera, 2.0);
    config.detector = BeaconDetectorConfig{};
    config.timestep_s = 0.02;  // 50 Hz fixed step
    config.camera_position_m = Vec3{0.0, 0.0, 0.0};
    config.initial_pan_rad = 0.0;
    config.initial_tilt_rad = 0.0;
    config.control_enabled = true;

    // Empirically-tuned MVP baseline (see ADR-010 / roadmap Step 7). The plant
    // (camera angle = integral of the rate command) already contains one
    // integrator, so a P-dominant law converges without oscillation:
    //   Ki = 0  -> no steady-state bias for a stationary target and no risk of
    //             integrator windup on the double-integrator that Ki would form;
    //   Kd = 0  -> the plant is already well damped; derivative would only
    //             amplify the ~0.02 px detector noise.
    // Output limit matches the camera actuator rate (deg_to_rad(30)).
    PIDAxisConfig axis{};
    axis.kp = 12.0;
    axis.ki = 0.0;
    axis.kd = 0.0;
    axis.integral_limit = 0.0;
    axis.output_limit_rad_s = config.camera.max_pan_rate_rad_s;  // == max_tilt_rate here
    config.controller.pan = axis;
    config.controller.tilt = axis;

    return config;
}

SimulationRunner::SimulationRunner(SimulationRunnerConfig config, const Trajectory& trajectory)
    : config_((config.validate(), std::move(config))),
      trajectory_(&trajectory),
      camera_(config_.camera, config_.camera_position_m, config_.initial_pan_rad,
              config_.initial_tilt_rad),
      renderer_(config_.renderer),
      detector_(config_.detector),
      controller_(config_.controller) {
    if (config_.perception_mode != PerceptionMode::Classical) {
        ai_detector_.emplace(*config_.ai_detector);
    }
    if (config_.tracker_enabled) {
        tracker_.emplace(config_.tracker);
    }
}

SimulationStepResult SimulationRunner::step() {
    SimulationStepResult result{};
    result.simulation_time_s = simulation_time_s_;
    result.frame_index = frame_index_;
    // Camera orientation that produces THIS frame's observation (before we step it).
    result.camera_pan_rad = camera_.pan_rad();
    result.camera_tilt_rad = camera_.tilt_rad();

    // 1. trajectory truth at the current sim time
    const TargetState target = trajectory_->state_at(simulation_time_s_);
    result.target_truth = target;

    // 2. observe the truth position through the current camera pose (exact projection)
    const CameraObservation observation = observe_beacon(camera_, target.position_m);
    result.observation = observation;
    result.target_visible = observation.visible();

    // 3. render the synthetic frame — the renderer sees ONLY the observation
    const cv::Mat clean_frame = renderer_.render(observation);
    // 3b. demo disturbance (Phase J, additive): default None makes this an
    // identity copy, applied AFTER the frozen renderer and BEFORE detection,
    // so both classical and AI see the SAME disturbed frame -- never reads
    // target/observation truth, only the already-rendered pixels. Seed is a
    // pure function of frame_index (deterministic, reproducible).
    const cv::Mat frame = config_.disturbance.kind == DemoDisturbanceKind::None
                               ? clean_frame
                               : apply_demo_disturbance(clean_frame, frame_index_, config_.disturbance);
    // Store the EXACT frame handed to the detector below (diagnostic/observer
    // only — a cheap refcounted cv::Mat copy, never read back into control).
    result.rendered_frame = frame;

    // 4. detect — MEASUREMENT path, pixels only. Classical mode never
    // constructs/runs the AI detector (ai_detector_ stays std::nullopt), so
    // this reduces to exactly the pre-Stage-3 call in that (default) mode.
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

    // 4b. state estimation / temporal gate (P0-v2, additive). Default
    // (tracker_enabled == false) leaves result.detection/target_detected
    // exactly as resolve_perception() produced them -- bit-identical to the
    // pre-tracker control path.
    if (config_.tracker_enabled) {
        const std::optional<ImagePoint> measurement =
            perception.detection.has_value()
                ? std::optional<ImagePoint>(ImagePoint{perception.detection->centroid_px.x_px,
                                                        perception.detection->centroid_px.y_px})
                : std::nullopt;
        result.tracked_state = tracker_->update(measurement, config_.timestep_s);
        if (is_safe_to_steer(result.tracked_state, config_.tracker_min_confidence_to_steer)) {
            result.detection =
                BeaconDetection{.centroid_px = {result.tracked_state.x_px, result.tracked_state.y_px}};
        } else {
            result.detection = std::nullopt;
        }
        result.target_detected = result.detection.has_value();
    }

    // 5. tracking error from the DETECTED (control-facing) centroid (never
    // observation.image_point_px)
    const std::optional<TrackingError> tracking_error =
        compute_tracking_error(result.detection, camera_);
    result.tracking_error = tracking_error;

    // 6. control, or the target-loss / open-loop policy
    ControlCommand command = zero_control_command();
    if (!config_.control_enabled) {
        controller_.reset();  // keep PID state clean while open-loop
    } else if (tracking_error.has_value()) {
        command = controller_.update(*tracking_error, config_.timestep_s);
    } else {
        // Target lost: reset the PID, command zero, camera holds its orientation.
        controller_.reset();
    }
    result.command = command;

    // 7. step the camera (the sole authority on pan/tilt state)
    result.applied_rates =
        camera_.step(command.pan_rate_rad_s, command.tilt_rate_rad_s, config_.timestep_s);

    // Diagnostic scoring: detected centroid vs exact projection (truth used ONLY here).
    if (result.detection.has_value() && observation.image_point_px.has_value()) {
        const double dx = result.detection->centroid_px.x_px - observation.image_point_px->x_px;
        const double dy = result.detection->centroid_px.y_px - observation.image_point_px->y_px;
        result.detection_error_px = std::hypot(dx, dy);
    }

    // 8/9. advance the clock
    simulation_time_s_ += config_.timestep_s;
    ++frame_index_;
    return result;
}

std::vector<SimulationStepResult> SimulationRunner::run_for(const double duration_s) {
    if (!std::isfinite(duration_s) || duration_s <= 0.0) {
        throw std::invalid_argument("SimulationRunner::run_for: duration_s must be finite and > 0.");
    }
    const auto step_count =
        static_cast<std::size_t>(std::ceil(duration_s / config_.timestep_s));
    std::vector<SimulationStepResult> results;
    results.reserve(step_count);
    for (std::size_t i = 0; i < step_count; ++i) {
        results.push_back(step());
    }
    return results;
}

void SimulationRunner::reset() {
    camera_ = PanTiltCamera{config_.camera, config_.camera_position_m, config_.initial_pan_rad,
                            config_.initial_tilt_rad};
    controller_.reset();
    if (tracker_.has_value()) {
        tracker_->reset();
    }
    simulation_time_s_ = 0.0;
    frame_index_ = 0;
}

double total_angular_error_rad(const TrackingError& error) noexcept {
    return std::hypot(error.angular.pan_rad, error.angular.tilt_rad);
}

SimulationMetrics evaluate(const std::vector<SimulationStepResult>& results) {
    SimulationMetrics metrics{};
    metrics.frame_count = results.size();
    if (results.empty()) {
        return metrics;
    }

    double sum_sq_angular = 0.0;
    double sum_detection_error_px = 0.0;
    std::size_t detection_error_samples = 0;

    for (const SimulationStepResult& r : results) {
        if (r.target_visible) {
            ++metrics.visible_frames;
        }
        if (r.detection.has_value()) {
            ++metrics.detected_frames;
        } else {
            ++metrics.lost_frames;
        }
        if (r.tracking_error.has_value()) {
            const double e = total_angular_error_rad(*r.tracking_error);
            sum_sq_angular += e * e;
            metrics.max_angular_error_rad = std::max(metrics.max_angular_error_rad, e);
            metrics.final_angular_error_rad = e;  // last detected frame wins
        }
        if (r.detection_error_px.has_value()) {
            sum_detection_error_px += *r.detection_error_px;
            ++detection_error_samples;
        }
    }

    metrics.detection_fraction =
        static_cast<double>(metrics.detected_frames) / static_cast<double>(metrics.frame_count);
    if (metrics.detected_frames > 0) {
        metrics.rms_angular_error_rad =
            std::sqrt(sum_sq_angular / static_cast<double>(metrics.detected_frames));
    }
    if (detection_error_samples > 0) {
        metrics.mean_detection_error_px =
            sum_detection_error_px / static_cast<double>(detection_error_samples);
    }
    return metrics;
}

}  // namespace fsoc
