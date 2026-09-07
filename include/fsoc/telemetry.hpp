#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fsoc/simulation_runner.hpp"

namespace fsoc {

// ===========================================================================
// Telemetry + benchmarking  (Step 8 — an OBSERVER of the closed loop)
// ===========================================================================
//
// Non-interference guarantee (frozen): everything in this header is a SINK. It
// consumes `SimulationStepResult` values and never calls back into the runner,
// PID, camera, detector, renderer, or trajectory. Running a simulation with or
// without telemetry produces a bit-identical `SimulationStepResult` sequence.

// Telemetry-facing tracking state. Deliberately two-valued: the runner has no
// deterministic "acquiring" phase, and saturation-while-tracking is already
// exposed via the *_saturated flags, so an "Acquiring" state would be
// decorative. Add one only when the system defines it.
enum class TrackingState {
    Tracking,    // a valid detection produced a TrackingError this frame
    TargetLost,  // no detection this frame
};

[[nodiscard]] std::string_view to_string(TrackingState state) noexcept;

// ---------------------------------------------------------------------------
// TelemetryRecord — one flat, explicitly-named, JSON-mappable frame record.
// ---------------------------------------------------------------------------
//
// Optional fields hold std::nullopt when the measurement is unavailable; there
// is NO in-memory sentinel value (no -1, no NaN). See docs/08_TELEMETRY_SCHEMA.md
// for name / type / unit / meaning / availability of every field.
struct TelemetryRecord {
    double simulation_time_s{};
    std::size_t frame_index{};

    bool target_visible{};   // TRUTH: target inside the configured FOV
    bool target_detected{};  // MEASUREMENT: detector returned a centroid

    // TRUTH: target world state (always present).
    double target_position_x_m{};
    double target_position_y_m{};
    double target_position_z_m{};
    double target_velocity_x_mps{};
    double target_velocity_y_mps{};
    double target_velocity_z_mps{};

    // MEASUREMENT: detected beacon centroid (present only when target_detected).
    std::optional<double> detected_x_px{};
    std::optional<double> detected_y_px{};

    // MEASUREMENT: image-plane tracking error (present only when a TrackingError exists).
    std::optional<double> pixel_error_x_px{};
    std::optional<double> pixel_error_y_px{};

    // MEASUREMENT: angular tracking error (present only when a TrackingError exists).
    std::optional<double> angular_error_pan_rad{};
    std::optional<double> angular_error_tilt_rad{};
    std::optional<double> angular_error_total_rad{};  // hypot(pan, tilt)

    // Camera orientation that produced this frame's observation (always present).
    double camera_pan_rad{};
    double camera_tilt_rad{};

    // Controller output (always present; {0,0} on loss / open-loop).
    double command_pan_rate_rad_s{};
    double command_tilt_rate_rad_s{};

    // Actuator-applied rate after saturation (always present).
    double applied_pan_rate_rad_s{};
    double applied_tilt_rate_rad_s{};

    // The axis is running at the actuator rate limit this frame (slewing flat out).
    // True when |command_*_rate| >= actuator max rate (within kSaturationTolerance_rad_s):
    // this catches both "PID demanded its output limit" and "camera clipped the command",
    // since the baseline sets the PID output limit equal to the actuator rate.
    bool pan_saturated{};
    bool tilt_saturated{};

    // DIAGNOSTIC: detected centroid vs exact projection (present only when both exist).
    std::optional<double> detection_error_px{};

    TrackingState tracking_state{TrackingState::TargetLost};

    // --- Perception diagnostics (Stage 3/4 AI + Safe Hybrid, additive) ---
    // Mirrors fsoc::PerceptionDiagnostics on SimulationStepResult (see
    // fsoc/perception.hpp, ADR-018). Always present; DIAGNOSTIC ONLY — never
    // fed back into the controller. `perception_mode` is CLASSICAL for every
    // pre-Stage-3 run, so old readers of this schema see a constant column.
    std::string perception_mode{"CLASSICAL"};              // CLASSICAL | AI | HYBRID
    std::string perception_source{"NONE"};                 // NONE | CLASSICAL | AI | HYBRID_AGREEMENT
    bool ai_candidate_detected{false};
    std::optional<double> ai_presence_probability{};        // sigmoid(presence_logit), present iff a candidate exists
    std::optional<double> ai_inference_ms{};                 // present iff a candidate exists
    std::optional<double> classical_ai_distance_px{};        // present iff both classical and AI produced a candidate
    std::string perception_rejection_reason{"NOT_APPLICABLE"};  // meaningful iff tracking_state == TargetLost under Hybrid

    // --- State estimation / temporal gate diagnostics (P0-v2, additive) ---
    // Mirrors fsoc::TrackedState on SimulationStepResult (see
    // fsoc/target_tracker.hpp). Always present; DIAGNOSTIC ONLY. When
    // tracker_enabled is false (default), the tracker never ran this run and
    // every reader sees the constant "SEARCHING" / absent-position row below
    // for every frame -- the exact same signature as a pre-tracker run.
    std::string tracker_lock_state{"SEARCHING"};             // SEARCHING | ACQUIRING | TRACKING | COASTING | LOST
    std::optional<double> tracker_x_px{};                     // present iff lock_state has a position estimate
    std::optional<double> tracker_y_px{};                     // (Acquiring, Tracking, or Coasting)
    std::optional<double> tracker_vx_px_s{};                  // present under the same condition
    std::optional<double> tracker_vy_px_s{};
    std::optional<double> tracker_confidence{};                // present under the same condition; in [0,1]
    bool tracker_is_prediction{false};                          // true iff this frame's position is a coast/predict, not a real measurement
    std::size_t tracker_coast_frames{0};                        // consecutive frames currently coasting (0 when not coasting)
};

// Tolerance for the saturation flags (rad/s).
inline constexpr double kSaturationTolerance_rad_s = 1e-9;

// Pure conversion. No side effects, no I/O, no reach-back into the loop.
// The actuator rate limits (from the same CameraConfig the runner used) are
// passed in so pan_saturated/tilt_saturated can be computed without the record
// carrying the whole camera config.
[[nodiscard]] TelemetryRecord make_telemetry_record(
    const SimulationStepResult& result,
    double max_pan_rate_rad_s,
    double max_tilt_rate_rad_s);

// ---------------------------------------------------------------------------
// CsvTelemetryLogger — synchronous, single-threaded, std::ofstream only.
// ---------------------------------------------------------------------------
class CsvTelemetryLogger {
public:
    // Opens `path` for writing (creating parent directories if needed). No
    // header is written yet — call write_header() once before record().
    explicit CsvTelemetryLogger(std::filesystem::path path);

    void write_header();
    void record(const TelemetryRecord& rec);

    [[nodiscard]] std::size_t records_written() const noexcept { return records_written_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // The CSV columns, in order. Also the stable JSON key list for a later frontend.
    [[nodiscard]] static const std::vector<std::string>& column_names();

private:
    std::filesystem::path path_;
    std::ofstream out_;
    std::size_t records_written_{0};
    bool header_written_{false};
};

// ---------------------------------------------------------------------------
// BenchmarkMetrics — richer than SimulationMetrics; computed from records only.
// ---------------------------------------------------------------------------
//
// Denominator conventions:
//   * angular / pixel error metrics: over frames with a TrackingError
//     (tracking_state == Tracking) -> denominator = tracking_frames.
//   * mean_detection_error_px: over frames where detection_error_px is present.
//   * saturation fractions and rate means/peaks: over ALL frames.
//   * detection_fraction: detected_frames / frames.
//   * final_angular_error_rad: the last tracking frame's total angular error.
struct BenchmarkMetrics {
    std::size_t frames{};
    std::size_t detected_frames{};
    std::size_t lost_frames{};
    std::size_t tracking_frames{};  // frames with a TrackingError
    double detection_fraction{};

    double rms_angular_error_rad{};
    double mean_angular_error_rad{};
    double max_angular_error_rad{};
    double final_angular_error_rad{};
    double p95_angular_error_rad{};  // nearest-rank: index = ceil(0.95*N) - 1

    double mean_pixel_error_px{};  // over hypot(pixel_error_x, pixel_error_y)
    double rms_pixel_error_px{};
    double max_pixel_error_px{};

    double mean_detection_error_px{};

    double command_saturation_fraction{};  // frames with pan OR tilt saturated / frames
    double pan_saturation_fraction{};
    double tilt_saturation_fraction{};

    double mean_abs_pan_rate_rad_s{};    // over applied_pan_rate, all frames
    double mean_abs_tilt_rate_rad_s{};
    double peak_applied_pan_rate_rad_s{};
    double peak_applied_tilt_rate_rad_s{};

    // Performance — WALL CLOCK, never the simulation timestep.
    double wall_execution_time_s{};
    double processing_fps{};  // frames / wall_execution_time_s
};

// `wall_execution_time_s` is measured by the caller with std::chrono around the
// step loop only; it never influences the simulation.
[[nodiscard]] BenchmarkMetrics compute_benchmark_metrics(
    const std::vector<TelemetryRecord>& records,
    double wall_execution_time_s);

// ---------------------------------------------------------------------------
// run_and_record — convenience: step the runner, time ONLY the step loop with
// steady_clock, then convert to TelemetryRecords (conversion is not timed).
// ---------------------------------------------------------------------------
struct RecordedRun {
    std::vector<SimulationStepResult> step_results;
    std::vector<TelemetryRecord> telemetry;
    double wall_execution_time_s{};

    [[nodiscard]] std::size_t frames() const noexcept { return step_results.size(); }
    [[nodiscard]] double processing_fps() const noexcept {
        return wall_execution_time_s > 0.0
                   ? static_cast<double>(step_results.size()) / wall_execution_time_s
                   : 0.0;
    }
};

[[nodiscard]] RecordedRun run_and_record(SimulationRunner& runner, double duration_s);

}  // namespace fsoc
