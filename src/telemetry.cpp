#include "fsoc/telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <system_error>

namespace fsoc {

std::string_view to_string(const TrackingState state) noexcept {
    switch (state) {
        case TrackingState::Tracking:
            return "Tracking";
        case TrackingState::TargetLost:
            return "TargetLost";
    }
    return "TargetLost";
}

TelemetryRecord make_telemetry_record(
    const SimulationStepResult& result,
    const double max_pan_rate_rad_s,
    const double max_tilt_rate_rad_s) {
    TelemetryRecord record{};

    record.simulation_time_s = result.simulation_time_s;
    record.frame_index = result.frame_index;

    record.target_visible = result.target_visible;
    record.target_detected = result.target_detected;

    record.target_position_x_m = result.target_truth.position_m.x;
    record.target_position_y_m = result.target_truth.position_m.y;
    record.target_position_z_m = result.target_truth.position_m.z;
    record.target_velocity_x_mps = result.target_truth.velocity_mps.x;
    record.target_velocity_y_mps = result.target_truth.velocity_mps.y;
    record.target_velocity_z_mps = result.target_truth.velocity_mps.z;

    if (result.detection.has_value()) {
        record.detected_x_px = result.detection->centroid_px.x_px;
        record.detected_y_px = result.detection->centroid_px.y_px;
    }

    if (result.tracking_error.has_value()) {
        const TrackingError& e = *result.tracking_error;
        record.pixel_error_x_px = e.pixel.x_px;
        record.pixel_error_y_px = e.pixel.y_px;
        record.angular_error_pan_rad = e.angular.pan_rad;
        record.angular_error_tilt_rad = e.angular.tilt_rad;
        record.angular_error_total_rad = std::hypot(e.angular.pan_rad, e.angular.tilt_rad);
    }

    record.camera_pan_rad = result.camera_pan_rad;
    record.camera_tilt_rad = result.camera_tilt_rad;

    record.command_pan_rate_rad_s = result.command.pan_rate_rad_s;
    record.command_tilt_rate_rad_s = result.command.tilt_rate_rad_s;
    record.applied_pan_rate_rad_s = result.applied_rates.pan_rate_rad_s;
    record.applied_tilt_rate_rad_s = result.applied_rates.tilt_rate_rad_s;

    // Flat-out slew: the commanded rate reached the actuator limit (or the camera
    // had to clip it), which the baseline's PID-limit == actuator-rate makes
    // equivalent to "|command| is at the actuator maximum".
    record.pan_saturated = std::abs(result.command.pan_rate_rad_s) >=
                           max_pan_rate_rad_s - kSaturationTolerance_rad_s;
    record.tilt_saturated = std::abs(result.command.tilt_rate_rad_s) >=
                            max_tilt_rate_rad_s - kSaturationTolerance_rad_s;

    record.detection_error_px = result.detection_error_px;

    record.tracking_state =
        result.tracking_error.has_value() ? TrackingState::Tracking : TrackingState::TargetLost;

    record.perception_mode = std::string(to_string(result.perception.perception_mode));
    record.perception_source = std::string(to_string(result.perception.perception_source));
    record.ai_candidate_detected = result.perception.ai_candidate_detected;
    record.ai_presence_probability = result.perception.ai_presence_probability;
    record.ai_inference_ms = result.perception.ai_inference_ms;
    record.classical_ai_distance_px = result.perception.classical_ai_distance_px;
    record.perception_rejection_reason = std::string(to_string(result.perception.rejection_reason));

    return record;
}

// ---------------------------------------------------------------------------
// CsvTelemetryLogger
// ---------------------------------------------------------------------------

const std::vector<std::string>& CsvTelemetryLogger::column_names() {
    static const std::vector<std::string> columns = {
        "simulation_time_s",
        "frame_index",
        "target_visible",
        "target_detected",
        "target_position_x_m",
        "target_position_y_m",
        "target_position_z_m",
        "target_velocity_x_mps",
        "target_velocity_y_mps",
        "target_velocity_z_mps",
        "detected_x_px",
        "detected_y_px",
        "pixel_error_x_px",
        "pixel_error_y_px",
        "angular_error_pan_rad",
        "angular_error_tilt_rad",
        "angular_error_total_rad",
        "camera_pan_rad",
        "camera_tilt_rad",
        "command_pan_rate_rad_s",
        "command_tilt_rate_rad_s",
        "applied_pan_rate_rad_s",
        "applied_tilt_rate_rad_s",
        "pan_saturated",
        "tilt_saturated",
        "detection_error_px",
        "tracking_state",
        "perception_mode",
        "perception_source",
        "ai_candidate_detected",
        "ai_presence_probability",
        "ai_inference_ms",
        "classical_ai_distance_px",
        "perception_rejection_reason",
    };
    return columns;
}

CsvTelemetryLogger::CsvTelemetryLogger(std::filesystem::path path) : path_(std::move(path)) {
    if (const std::filesystem::path parent = path_.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }
    out_.open(path_, std::ios::out | std::ios::trunc);
    if (!out_.is_open()) {
        throw std::runtime_error("CsvTelemetryLogger: cannot open '" + path_.string() + "'.");
    }
    out_ << std::setprecision(10);
}

void CsvTelemetryLogger::write_header() {
    const std::vector<std::string>& columns = column_names();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        out_ << columns[i];
        if (i + 1 < columns.size()) {
            out_ << ',';
        }
    }
    out_ << '\n';
    out_.flush();
    header_written_ = true;
}

namespace {

// Writes an optional double: the number, or an empty field for std::nullopt.
void put_optional(std::ostream& out, const std::optional<double>& value) {
    if (value.has_value()) {
        out << *value;
    }
}

}  // namespace

void CsvTelemetryLogger::record(const TelemetryRecord& rec) {
    if (!header_written_) {
        write_header();
    }

    out_ << rec.simulation_time_s << ',' << rec.frame_index << ','
         << (rec.target_visible ? 1 : 0) << ',' << (rec.target_detected ? 1 : 0) << ','
         << rec.target_position_x_m << ',' << rec.target_position_y_m << ','
         << rec.target_position_z_m << ',' << rec.target_velocity_x_mps << ','
         << rec.target_velocity_y_mps << ',' << rec.target_velocity_z_mps << ',';
    put_optional(out_, rec.detected_x_px);
    out_ << ',';
    put_optional(out_, rec.detected_y_px);
    out_ << ',';
    put_optional(out_, rec.pixel_error_x_px);
    out_ << ',';
    put_optional(out_, rec.pixel_error_y_px);
    out_ << ',';
    put_optional(out_, rec.angular_error_pan_rad);
    out_ << ',';
    put_optional(out_, rec.angular_error_tilt_rad);
    out_ << ',';
    put_optional(out_, rec.angular_error_total_rad);
    out_ << ',' << rec.camera_pan_rad << ',' << rec.camera_tilt_rad << ','
         << rec.command_pan_rate_rad_s << ',' << rec.command_tilt_rate_rad_s << ','
         << rec.applied_pan_rate_rad_s << ',' << rec.applied_tilt_rate_rad_s << ','
         << (rec.pan_saturated ? 1 : 0) << ',' << (rec.tilt_saturated ? 1 : 0) << ',';
    put_optional(out_, rec.detection_error_px);
    out_ << ',' << to_string(rec.tracking_state) << ',' << rec.perception_mode << ','
         << rec.perception_source << ',' << (rec.ai_candidate_detected ? 1 : 0) << ',';
    put_optional(out_, rec.ai_presence_probability);
    out_ << ',';
    put_optional(out_, rec.ai_inference_ms);
    out_ << ',';
    put_optional(out_, rec.classical_ai_distance_px);
    out_ << ',' << rec.perception_rejection_reason << '\n';
    out_.flush();  // synchronous: the file is always current on disk

    ++records_written_;
}

// ---------------------------------------------------------------------------
// BenchmarkMetrics
// ---------------------------------------------------------------------------

BenchmarkMetrics compute_benchmark_metrics(
    const std::vector<TelemetryRecord>& records,
    const double wall_execution_time_s) {
    BenchmarkMetrics metrics{};
    metrics.frames = records.size();
    metrics.wall_execution_time_s = wall_execution_time_s;
    metrics.processing_fps =
        wall_execution_time_s > 0.0
            ? static_cast<double>(records.size()) / wall_execution_time_s
            : 0.0;
    if (records.empty()) {
        return metrics;
    }

    std::vector<double> angular_magnitudes;  // for percentile
    angular_magnitudes.reserve(records.size());

    double sum_sq_angular = 0.0;
    double sum_angular = 0.0;
    double sum_sq_pixel = 0.0;
    double sum_pixel = 0.0;
    double sum_detection_error_px = 0.0;
    std::size_t detection_error_samples = 0;
    double sum_abs_pan_rate = 0.0;
    double sum_abs_tilt_rate = 0.0;
    std::size_t command_saturated = 0;
    std::size_t pan_saturated = 0;
    std::size_t tilt_saturated = 0;

    for (const TelemetryRecord& r : records) {
        if (r.target_detected) {
            ++metrics.detected_frames;
        } else {
            ++metrics.lost_frames;
        }

        if (r.angular_error_total_rad.has_value()) {
            ++metrics.tracking_frames;
            const double a = *r.angular_error_total_rad;
            angular_magnitudes.push_back(a);
            sum_angular += a;
            sum_sq_angular += a * a;
            metrics.max_angular_error_rad = std::max(metrics.max_angular_error_rad, a);
            metrics.final_angular_error_rad = a;  // last tracking frame wins

            const double px = std::hypot(r.pixel_error_x_px.value_or(0.0),
                                         r.pixel_error_y_px.value_or(0.0));
            sum_pixel += px;
            sum_sq_pixel += px * px;
            metrics.max_pixel_error_px = std::max(metrics.max_pixel_error_px, px);
        }

        if (r.detection_error_px.has_value()) {
            sum_detection_error_px += *r.detection_error_px;
            ++detection_error_samples;
        }

        sum_abs_pan_rate += std::abs(r.applied_pan_rate_rad_s);
        sum_abs_tilt_rate += std::abs(r.applied_tilt_rate_rad_s);
        metrics.peak_applied_pan_rate_rad_s =
            std::max(metrics.peak_applied_pan_rate_rad_s, std::abs(r.applied_pan_rate_rad_s));
        metrics.peak_applied_tilt_rate_rad_s =
            std::max(metrics.peak_applied_tilt_rate_rad_s, std::abs(r.applied_tilt_rate_rad_s));

        if (r.pan_saturated || r.tilt_saturated) {
            ++command_saturated;
        }
        if (r.pan_saturated) {
            ++pan_saturated;
        }
        if (r.tilt_saturated) {
            ++tilt_saturated;
        }
    }

    const auto frames_d = static_cast<double>(metrics.frames);
    metrics.detection_fraction = static_cast<double>(metrics.detected_frames) / frames_d;
    metrics.command_saturation_fraction = static_cast<double>(command_saturated) / frames_d;
    metrics.pan_saturation_fraction = static_cast<double>(pan_saturated) / frames_d;
    metrics.tilt_saturation_fraction = static_cast<double>(tilt_saturated) / frames_d;
    metrics.mean_abs_pan_rate_rad_s = sum_abs_pan_rate / frames_d;
    metrics.mean_abs_tilt_rate_rad_s = sum_abs_tilt_rate / frames_d;

    if (metrics.tracking_frames > 0) {
        const auto n = static_cast<double>(metrics.tracking_frames);
        metrics.mean_angular_error_rad = sum_angular / n;
        metrics.rms_angular_error_rad = std::sqrt(sum_sq_angular / n);
        metrics.mean_pixel_error_px = sum_pixel / n;
        metrics.rms_pixel_error_px = std::sqrt(sum_sq_pixel / n);

        std::sort(angular_magnitudes.begin(), angular_magnitudes.end());
        // nearest-rank: index = ceil(0.95 * N) - 1, clamped into [0, N-1].
        const auto rank = static_cast<std::size_t>(
            std::ceil(0.95 * static_cast<double>(angular_magnitudes.size())));
        const std::size_t index = rank > 0 ? rank - 1 : 0;
        metrics.p95_angular_error_rad =
            angular_magnitudes[std::min(index, angular_magnitudes.size() - 1)];
    }

    if (detection_error_samples > 0) {
        metrics.mean_detection_error_px =
            sum_detection_error_px / static_cast<double>(detection_error_samples);
    }

    return metrics;
}

// ---------------------------------------------------------------------------
// run_and_record
// ---------------------------------------------------------------------------

RecordedRun run_and_record(SimulationRunner& runner, const double duration_s) {
    if (!std::isfinite(duration_s) || duration_s <= 0.0) {
        throw std::invalid_argument("run_and_record: duration_s must be finite and > 0.");
    }
    const auto step_count = static_cast<std::size_t>(
        std::ceil(duration_s / runner.config().timestep_s));

    RecordedRun run{};
    run.step_results.reserve(step_count);

    // Time ONLY the simulation step loop. Telemetry conversion below is not timed.
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < step_count; ++i) {
        run.step_results.push_back(runner.step());
    }
    const auto end = std::chrono::steady_clock::now();
    run.wall_execution_time_s = std::chrono::duration<double>(end - start).count();

    const double max_pan_rate_rad_s = runner.config().camera.max_pan_rate_rad_s;
    const double max_tilt_rate_rad_s = runner.config().camera.max_tilt_rate_rad_s;
    run.telemetry.reserve(step_count);
    for (const SimulationStepResult& r : run.step_results) {
        run.telemetry.push_back(make_telemetry_record(r, max_pan_rate_rad_s, max_tilt_rate_rad_s));
    }
    return run;
}

}  // namespace fsoc
