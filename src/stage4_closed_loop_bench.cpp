#include "fsoc/stage4_closed_loop_bench.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

#include "fsoc/camera.hpp"
#include "fsoc/config.hpp"
#include "fsoc/observation.hpp"
#include "fsoc/pid_controller.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/stage4_degradation.hpp"
#include "fsoc/stage4_metrics.hpp"
#include "fsoc/target_state.hpp"
#include "fsoc/tracking_error.hpp"
#include "fsoc/trajectory.hpp"

namespace fsoc::stage4 {

namespace {

void tally_hybrid_source(const fsoc::PerceptionResult& perception, HybridSourceCounts& sources) {
    switch (perception.diagnostics.perception_source) {
        case fsoc::PerceptionSource::HybridAgreement:
            ++sources.hybrid_agreement;
            return;
        case fsoc::PerceptionSource::Classical:
            ++sources.classical;
            return;
        case fsoc::PerceptionSource::AI:
            return;  // never emitted under Hybrid (ADR-018)
        case fsoc::PerceptionSource::None:
            switch (perception.diagnostics.rejection_reason) {
                case fsoc::PerceptionRejectionReason::AiOnlyUnverified:
                    ++sources.ai_only_unverified;
                    return;
                case fsoc::PerceptionRejectionReason::DetectorDisagreement:
                    ++sources.detector_disagreement;
                    return;
                case fsoc::PerceptionRejectionReason::NotApplicable:
                    ++sources.none_not_applicable;
                    return;
            }
            return;
    }
}

}  // namespace

ClosedLoopRawSeedResult run_closed_loop_scenario_mode_seed(
    const ScenarioId scenario, const fsoc::PerceptionMode mode, const std::uint64_t base_seed,
    const std::size_t seed_index, const ClosedLoopBenchConfig& bench_config,
    const fsoc::BeaconDetector& classical_detector, const fsoc::AiBeaconDetector& ai_detector,
    const std::optional<fsoc::TargetTrackerConfig> tracker_config,
    const double tracker_min_confidence_to_steer) {
    std::optional<fsoc::TargetTracker> tracker;
    if (tracker_config.has_value()) {
        tracker.emplace(*tracker_config);
    }
    const fsoc::CameraConfig camera_config{};
    fsoc::PanTiltCamera camera{camera_config, fsoc::Vec3{0.0, 0.0, 0.0}, 0.0, 0.0};
    const fsoc::SyntheticCameraRenderer renderer{closed_loop_renderer_config(scenario)};
    const fsoc::StationaryTrajectory trajectory{closed_loop_target_position_m(scenario)};
    const DegradationConfig degradation_config = closed_loop_degradation_config(scenario, seed_index);
    const int idx = scenario_index(scenario);

    fsoc::PIDAxisConfig axis{};
    axis.kp = 12.0;
    axis.ki = 0.0;
    axis.kd = 0.0;
    axis.integral_limit = 0.0;
    axis.output_limit_rad_s = camera_config.max_pan_rate_rad_s;  // == max_tilt_rate_rad_s here
    fsoc::PIDControllerConfig pid_config{};
    pid_config.pan = axis;
    pid_config.tilt = axis;
    fsoc::PIDController pid{pid_config};

    const auto step_count = static_cast<std::size_t>(std::ceil(bench_config.duration_s / bench_config.dt_s));

    ClosedLoopRawSeedResult raw{};
    raw.total_frames = step_count;

    std::size_t current_loss_streak = 0;
    bool was_lost_before = false;
    double sim_time_s = 0.0;
    constexpr double kSaturationEps = 1e-9;

    for (std::size_t frame_index = 0; frame_index < step_count; ++frame_index) {
        const fsoc::TargetState target = trajectory.state_at(sim_time_s);
        const fsoc::CameraObservation observation = fsoc::observe_beacon(camera, target.position_m);
        const cv::Mat clean_frame = renderer.render(observation);
        const std::uint64_t degradation_seed = frame_seed(idx, base_seed, static_cast<std::uint64_t>(frame_index));
        const cv::Mat frame = apply_degradation(clean_frame, degradation_seed, degradation_config);

        std::optional<fsoc::BeaconDetection> classical_detection{};
        std::optional<fsoc::AiBeaconDetection> ai_detection{};
        double classical_ms = 0.0;
        double ai_ms = 0.0;

        if (mode != fsoc::PerceptionMode::AI) {
            const auto t0 = std::chrono::steady_clock::now();
            classical_detection = classical_detector.detect(frame);
            const auto t1 = std::chrono::steady_clock::now();
            classical_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        if (mode != fsoc::PerceptionMode::Classical) {
            const auto t0 = std::chrono::steady_clock::now();
            ai_detection = ai_detector.detect(frame);
            const auto t1 = std::chrono::steady_clock::now();
            ai_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        }

        const auto rp_t0 = std::chrono::steady_clock::now();
        const fsoc::PerceptionResult perception = fsoc::resolve_perception(mode, classical_detection, ai_detection);
        const auto rp_t1 = std::chrono::steady_clock::now();
        const double resolve_ms = std::chrono::duration<double, std::milli>(rp_t1 - rp_t0).count();

        if (frame_index >= bench_config.warmup_frames_for_latency) {
            double total_ms = 0.0;
            switch (mode) {
                case fsoc::PerceptionMode::Classical:
                    total_ms = classical_ms;
                    break;
                case fsoc::PerceptionMode::AI:
                    total_ms = ai_ms;
                    break;
                case fsoc::PerceptionMode::Hybrid:
                    total_ms = classical_ms + ai_ms + resolve_ms;
                    break;
            }
            raw.perception_latency_ms.push_back(total_ms);
        }

        if (mode == fsoc::PerceptionMode::Hybrid) {
            tally_hybrid_source(perception, raw.hybrid_sources);
        }

        // P0-v2 (Phase E/F): when a tracker is configured, it is layered AFTER
        // resolve_perception() -- final_detection (not perception.detection)
        // is what every metric below scores, exactly matching what
        // fsoc::SimulationRunner::step() hands to the controller.
        std::optional<fsoc::BeaconDetection> final_detection = perception.detection;
        if (tracker.has_value()) {
            const std::optional<fsoc::ImagePoint> measurement =
                perception.detection.has_value()
                    ? std::optional<fsoc::ImagePoint>(perception.detection->centroid_px)
                    : std::nullopt;
            const fsoc::TrackedState tracked = tracker->update(measurement, bench_config.dt_s);
            final_detection = fsoc::is_safe_to_steer(tracked, tracker_min_confidence_to_steer)
                                   ? std::optional<fsoc::BeaconDetection>(
                                         fsoc::BeaconDetection{.centroid_px = {tracked.x_px, tracked.y_px}})
                                   : std::nullopt;
        }

        if (final_detection.has_value()) {
            ++raw.accepted_frames;
        } else {
            ++raw.target_lost_frames;
        }

        // Evaluator-only, truth-scored control-outlier safety metric.
        if (final_detection.has_value() && observation.image_point_px.has_value()) {
            const double dx = final_detection->centroid_px.x_px - observation.image_point_px->x_px;
            const double dy = final_detection->centroid_px.y_px - observation.image_point_px->y_px;
            const double error_px = std::hypot(dx, dy);
            raw.max_control_error_px = std::max(raw.max_control_error_px, error_px);
            if (error_px > 20.0) ++raw.control_outlier_gt20;
            if (error_px > 50.0) ++raw.control_outlier_gt50;
            if (error_px > 100.0) ++raw.control_outlier_gt100;
        }

        const std::optional<fsoc::TrackingError> tracking_error =
            fsoc::compute_tracking_error(final_detection, camera);

        const bool tracking_now = tracking_error.has_value();
        if (!tracking_now) {
            ++current_loss_streak;
            raw.longest_loss_streak = std::max(raw.longest_loss_streak, current_loss_streak);
            was_lost_before = true;
        } else {
            if (was_lost_before && current_loss_streak > 0) {
                raw.reacquisition_times_frames.push_back(static_cast<double>(current_loss_streak));
            }
            current_loss_streak = 0;
            was_lost_before = false;
            raw.angular_errors_rad.push_back(
                std::hypot(tracking_error->angular.pan_rad, tracking_error->angular.tilt_rad));
        }

        fsoc::ControlCommand command = fsoc::zero_control_command();
        if (tracking_error.has_value()) {
            command = pid.update(*tracking_error, bench_config.dt_s);
        } else {
            pid.reset();
        }

        const fsoc::AppliedRates applied = camera.step(command.pan_rate_rad_s, command.tilt_rate_rad_s, bench_config.dt_s);

        raw.max_commanded_pan_rate_rad_s = std::max(raw.max_commanded_pan_rate_rad_s, std::abs(command.pan_rate_rad_s));
        raw.max_commanded_tilt_rate_rad_s =
            std::max(raw.max_commanded_tilt_rate_rad_s, std::abs(command.tilt_rate_rad_s));
        if (std::abs(applied.pan_rate_rad_s) >= camera_config.max_pan_rate_rad_s - kSaturationEps ||
            std::abs(applied.tilt_rate_rad_s) >= camera_config.max_tilt_rate_rad_s - kSaturationEps) {
            ++raw.saturated_actuator_frames;
        }

        sim_time_s += bench_config.dt_s;
    }

    return raw;
}

ClosedLoopScenarioModeResult finalize_closed_loop_results(
    const ScenarioId scenario, const fsoc::PerceptionMode mode,
    const std::vector<ClosedLoopRawSeedResult>& per_seed) {
    ClosedLoopScenarioModeResult out{};
    out.scenario = scenario;
    out.mode = mode;

    std::size_t total_frames = 0;
    std::size_t accepted = 0;
    std::size_t lost = 0;
    std::size_t longest = 0;
    std::size_t saturated = 0;
    double max_pan = 0.0;
    double max_tilt = 0.0;
    HybridSourceCounts sources{};
    std::size_t gt20 = 0;
    std::size_t gt50 = 0;
    std::size_t gt100 = 0;
    double max_ctrl_error = 0.0;
    std::vector<double> pooled_angular_errors;
    std::vector<double> pooled_reacq_times;
    std::vector<double> pooled_latency;

    for (const ClosedLoopRawSeedResult& r : per_seed) {
        total_frames += r.total_frames;
        accepted += r.accepted_frames;
        lost += r.target_lost_frames;
        longest = std::max(longest, r.longest_loss_streak);
        saturated += r.saturated_actuator_frames;
        max_pan = std::max(max_pan, r.max_commanded_pan_rate_rad_s);
        max_tilt = std::max(max_tilt, r.max_commanded_tilt_rate_rad_s);
        sources.hybrid_agreement += r.hybrid_sources.hybrid_agreement;
        sources.classical += r.hybrid_sources.classical;
        sources.ai_only_unverified += r.hybrid_sources.ai_only_unverified;
        sources.detector_disagreement += r.hybrid_sources.detector_disagreement;
        sources.none_not_applicable += r.hybrid_sources.none_not_applicable;
        gt20 += r.control_outlier_gt20;
        gt50 += r.control_outlier_gt50;
        gt100 += r.control_outlier_gt100;
        max_ctrl_error = std::max(max_ctrl_error, r.max_control_error_px);
        pooled_angular_errors.insert(pooled_angular_errors.end(), r.angular_errors_rad.begin(), r.angular_errors_rad.end());
        pooled_reacq_times.insert(
            pooled_reacq_times.end(), r.reacquisition_times_frames.begin(), r.reacquisition_times_frames.end());
        pooled_latency.insert(pooled_latency.end(), r.perception_latency_ms.begin(), r.perception_latency_ms.end());
    }

    out.total_frames = total_frames;
    out.accepted_detection_fraction =
        total_frames > 0 ? static_cast<double>(accepted) / static_cast<double>(total_frames) : 0.0;
    out.target_lost_frames = lost;
    out.longest_loss_streak = longest;
    out.reacquisition_count = pooled_reacq_times.size();
    out.mean_reacquisition_time_frames =
        pooled_reacq_times.empty()
            ? 0.0
            : std::accumulate(pooled_reacq_times.begin(), pooled_reacq_times.end(), 0.0) /
                  static_cast<double>(pooled_reacq_times.size());
    out.saturated_actuator_frames = saturated;
    out.max_commanded_pan_rate_rad_s = max_pan;
    out.max_commanded_tilt_rate_rad_s = max_tilt;
    out.hybrid_sources = sources;
    out.control_outlier_gt20 = gt20;
    out.control_outlier_gt50 = gt50;
    out.control_outlier_gt100 = gt100;
    out.max_control_error_px = max_ctrl_error;

    if (!pooled_angular_errors.empty()) {
        std::vector<double> sorted = pooled_angular_errors;
        std::sort(sorted.begin(), sorted.end());
        double sum_sq = 0.0;
        for (const double e : sorted) {
            sum_sq += e * e;
        }
        const auto n = sorted.size();
        out.rms_angular_error_rad = std::sqrt(sum_sq / static_cast<double>(n));
        out.median_angular_error_rad = (n % 2 == 1) ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
        out.max_angular_error_rad = sorted.back();
        out.p95_angular_error_rad = percentile95_nearest_rank(sorted);
    }
    if (!pooled_latency.empty()) {
        double sum = 0.0;
        for (const double v : pooled_latency) {
            sum += v;
        }
        out.perception_latency_mean_ms = sum / static_cast<double>(pooled_latency.size());
        out.perception_latency_p95_ms = percentile95_nearest_rank(pooled_latency);
    }

    return out;
}

}  // namespace fsoc::stage4
