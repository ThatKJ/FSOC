// Step 8 deterministic unit checks: telemetry + benchmarking.
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/step7_tests.cpp.
// Links fsoc::telemetry (which transitively brings simulation/core/... + OpenCV).

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsoc/config.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/telemetry.hpp"
#include "fsoc/trajectory.hpp"

namespace {

using fsoc::BenchmarkMetrics;
using fsoc::compute_benchmark_metrics;
using fsoc::CsvTelemetryLogger;
using fsoc::make_telemetry_record;
using fsoc::SimulationStepResult;
using fsoc::TelemetryRecord;
using fsoc::TrackingState;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

void check_near(
    const double actual,
    const double expected,
    const double tolerance,
    const std::string_view expression,
    const int line) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << " actual=" << actual
                  << " expected=" << expected << " tol=" << tolerance << '\n';
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_NEAR(actual, expected, tol) \
    check_near((actual), (expected), (tol), #actual " ~= " #expected, __LINE__)

// ---- helpers -----------------------------------------------------

// Default CameraConfig actuator rate limit (both axes).
constexpr double kMaxRate = fsoc::deg_to_rad(30.0);

[[nodiscard]] TelemetryRecord to_rec(const SimulationStepResult& r) {
    return make_telemetry_record(r, kMaxRate, kMaxRate);
}

[[nodiscard]] SimulationStepResult base_result() {
    SimulationStepResult r{};
    r.simulation_time_s = 1.28;
    r.frame_index = 64;
    r.target_truth.position_m = {10.0, -20.0, 30.0};
    r.target_truth.velocity_mps = {1.0, -2.0, 3.0};
    r.target_visible = true;
    r.target_detected = true;
    r.detection = fsoc::BeaconDetection{.centroid_px = {123.5, 456.25}};
    fsoc::TrackingError e{};
    e.pixel = {80.0, -60.0};
    e.angular = {0.044, 0.033};
    r.tracking_error = e;
    r.command = {0.40, -0.20};
    r.applied_rates = {0.30, -0.20};
    r.camera_pan_rad = 0.10;
    r.camera_tilt_rad = -0.05;
    r.detection_error_px = 0.017;
    return r;
}

[[nodiscard]] TelemetryRecord tracking_rec(const double angular_total, const double px,
                                           const double py) {
    TelemetryRecord t{};
    t.target_detected = true;
    t.angular_error_pan_rad = angular_total;
    t.angular_error_tilt_rad = 0.0;
    t.angular_error_total_rad = angular_total;
    t.pixel_error_x_px = px;
    t.pixel_error_y_px = py;
    t.tracking_state = TrackingState::Tracking;
    return t;
}

[[nodiscard]] TelemetryRecord lost_rec() {
    TelemetryRecord t{};
    t.target_detected = false;
    t.tracking_state = TrackingState::TargetLost;
    return t;
}

[[nodiscard]] std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream{line};
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    // trailing empty field (line ends with ',') is not produced by getline; handle it.
    if (!line.empty() && line.back() == ',') {
        fields.emplace_back();
    }
    return fields;
}

[[nodiscard]] std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream in{path};
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] std::filesystem::path temp_csv(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// ---- 1..12. make_telemetry_record copies every field ------------

void test_record_scalar_copies() {
    const TelemetryRecord t = to_rec(base_result());

    CHECK(t.simulation_time_s == 1.28);                           // (1)
    CHECK(t.frame_index == 64);                                   // (2)

    CHECK(t.target_position_x_m == 10.0);                         // (3)
    CHECK(t.target_position_y_m == -20.0);
    CHECK(t.target_position_z_m == 30.0);
    CHECK(t.target_velocity_x_mps == 1.0);
    CHECK(t.target_velocity_y_mps == -2.0);
    CHECK(t.target_velocity_z_mps == 3.0);

    CHECK(t.detected_x_px.has_value() && *t.detected_x_px == 123.5);   // (4)
    CHECK(t.detected_y_px.has_value() && *t.detected_y_px == 456.25);

    CHECK(t.pixel_error_x_px.has_value() && *t.pixel_error_x_px == 80.0);   // (6)
    CHECK(t.pixel_error_y_px.has_value() && *t.pixel_error_y_px == -60.0);
    CHECK(t.angular_error_pan_rad.has_value() && *t.angular_error_pan_rad == 0.044);
    CHECK(t.angular_error_tilt_rad.has_value() && *t.angular_error_tilt_rad == 0.033);
    CHECK(t.angular_error_total_rad.has_value());
    CHECK_NEAR(*t.angular_error_total_rad, std::hypot(0.044, 0.033), 1e-15);

    CHECK(t.camera_pan_rad == 0.10);                              // (8)
    CHECK(t.camera_tilt_rad == -0.05);
    CHECK(t.command_pan_rate_rad_s == 0.40);                      // (9)
    CHECK(t.command_tilt_rate_rad_s == -0.20);
    CHECK(t.applied_pan_rate_rad_s == 0.30);                      // (10)
    CHECK(t.applied_tilt_rate_rad_s == -0.20);

    CHECK(t.detection_error_px.has_value() && *t.detection_error_px == 0.017);
    CHECK(t.tracking_state == TrackingState::Tracking);           // (12)
}

void test_record_lost_target_absences() {
    SimulationStepResult r = base_result();
    r.detection.reset();
    r.target_detected = false;
    r.tracking_error.reset();
    r.detection_error_px.reset();
    r.command = {0.0, 0.0};
    r.applied_rates = {0.0, 0.0};

    const TelemetryRecord t = to_rec(r);
    CHECK(!t.detected_x_px.has_value());                          // (5)
    CHECK(!t.detected_y_px.has_value());
    CHECK(!t.pixel_error_x_px.has_value());                       // (7)
    CHECK(!t.pixel_error_y_px.has_value());
    CHECK(!t.angular_error_pan_rad.has_value());
    CHECK(!t.angular_error_tilt_rad.has_value());
    CHECK(!t.angular_error_total_rad.has_value());
    CHECK(!t.detection_error_px.has_value());
    CHECK(t.tracking_state == TrackingState::TargetLost);         // (12)
    // truth is still present even when the target is lost.
    CHECK(t.target_position_x_m == 10.0);
}

void test_record_saturation_flags() {  // (11)
    // Flat-out slew: |command rate| at the actuator limit (kMaxRate) -> saturated.
    SimulationStepResult r = base_result();

    r.command = {kMaxRate, -0.10};  // pan at the limit, tilt well below
    TelemetryRecord t = to_rec(r);
    CHECK(t.pan_saturated);
    CHECK(!t.tilt_saturated);

    r.command = {0.20, -kMaxRate};  // tilt at the limit (negative direction)
    t = to_rec(r);
    CHECK(!t.pan_saturated);
    CHECK(t.tilt_saturated);

    // Just under the limit -> not saturated.
    r.command = {kMaxRate - 1e-3, kMaxRate - 1e-3};
    t = to_rec(r);
    CHECK(!t.pan_saturated);
    CHECK(!t.tilt_saturated);

    // Zero command (lost / open-loop) -> not saturated.
    r.command = {0.0, 0.0};
    t = to_rec(r);
    CHECK(!t.pan_saturated);
    CHECK(!t.tilt_saturated);
}

// ---- 13..16. CSV logger --------------------------------------

void test_csv_header_deterministic() {  // (13)
    const auto& a = CsvTelemetryLogger::column_names();
    const auto& b = CsvTelemetryLogger::column_names();
    CHECK(a == b);
    CHECK(a.size() == 34);  // 27 Step-8 core fields + 7 Stage-3 perception fields (additive, appended)
    CHECK(a.front() == "simulation_time_s");
    CHECK(a.at(26) == "tracking_state");         // last of the original 27
    CHECK(a.back() == "perception_rejection_reason");

    const std::filesystem::path p1 = temp_csv("fsoc_step8_hdr1.csv");
    const std::filesystem::path p2 = temp_csv("fsoc_step8_hdr2.csv");
    { CsvTelemetryLogger l1{p1}; l1.write_header(); }
    { CsvTelemetryLogger l2{p2}; l2.write_header(); }
    CHECK(read_lines(p1) == read_lines(p2));
    std::string expected;
    for (std::size_t i = 0; i < a.size(); ++i) {
        expected += a[i];
        if (i + 1 < a.size()) expected += ',';
    }
    CHECK(read_lines(p1).front() == expected);
    std::filesystem::remove(p1);
    std::filesystem::remove(p2);
}

void test_csv_column_count_and_empty_fields() {  // (14)(15)
    const std::filesystem::path p = temp_csv("fsoc_step8_cols.csv");
    {
        CsvTelemetryLogger logger{p};
        logger.write_header();
        logger.record(to_rec(base_result()));  // all optionals present

        SimulationStepResult lost = base_result();
        lost.detection.reset();
        lost.target_detected = false;
        lost.tracking_error.reset();
        lost.detection_error_px.reset();
        logger.record(to_rec(lost));  // optionals -> empty fields
    }
    const auto lines = read_lines(p);
    CHECK(lines.size() == 3);  // header + 2 records
    const std::size_t columns = CsvTelemetryLogger::column_names().size();
    CHECK(split_csv_line(lines[0]).size() == columns);
    CHECK(split_csv_line(lines[1]).size() == columns);
    CHECK(split_csv_line(lines[2]).size() == columns);

    // (15) the lost-target record's detected_x_px column (index 10) is empty.
    const auto lost_fields = split_csv_line(lines[2]);
    CHECK(lost_fields.at(10).empty());   // detected_x_px
    CHECK(lost_fields.at(11).empty());   // detected_y_px
    CHECK(lost_fields.at(14).empty());   // angular_error_pan_rad
    CHECK(lost_fields.at(16).empty());   // angular_error_total_rad
    CHECK(lost_fields.at(25).empty());   // detection_error_px
    CHECK(lost_fields.at(26) == "TargetLost");  // tracking_state (no longer the last column)

    // The full (Classical-mode) record has no empty fields among the original
    // 27 columns, but the 3 AI-diagnostic optionals (ai_presence_probability,
    // ai_inference_ms, classical_ai_distance_px) are legitimately empty:
    // Classical mode never produces an AI candidate, so there is nothing to
    // report there — this is the correct behaviour, not a missing value.
    const auto full_fields = split_csv_line(lines[1]);
    for (std::size_t i = 0; i < 27; ++i) {
        CHECK(!full_fields.at(i).empty());
    }
    // base_result() is a hand-built SimulationStepResult that never ran
    // resolve_perception(), so .perception is left at its struct default
    // (mode=Classical, source=None) regardless of .detection — this test is
    // about make_telemetry_record()'s field copy, not perception resolution.
    CHECK(full_fields.at(27) == "CLASSICAL");   // perception_mode
    CHECK(full_fields.at(28) == "NONE");        // perception_source (default; not resolved here)
    CHECK(full_fields.at(29) == "0");           // ai_candidate_detected
    CHECK(full_fields.at(30).empty());          // ai_presence_probability
    CHECK(full_fields.at(31).empty());          // ai_inference_ms
    CHECK(full_fields.at(32).empty());          // classical_ai_distance_px
    CHECK(full_fields.at(33) == "NOT_APPLICABLE");  // perception_rejection_reason
    std::filesystem::remove(p);
}

void test_csv_multiple_records() {  // (16)
    const std::filesystem::path p = temp_csv("fsoc_step8_multi.csv");
    CsvTelemetryLogger logger{p};
    logger.write_header();
    for (int i = 0; i < 17; ++i) {
        SimulationStepResult r = base_result();
        r.frame_index = static_cast<std::size_t>(i);
        logger.record(to_rec(r));
    }
    CHECK(logger.records_written() == 17);
    CHECK(read_lines(p).size() == 18);  // header + 17
    std::filesystem::remove(p);
}

// ---- 17..26. BenchmarkMetrics from a hand dataset ----------

void test_benchmark_counts_and_fraction() {  // (17)(18)(19)
    std::vector<TelemetryRecord> recs = {
        tracking_rec(0.01, 0, 0), tracking_rec(0.02, 0, 0), lost_rec(),
        tracking_rec(0.03, 0, 0), lost_rec()};
    const BenchmarkMetrics m = compute_benchmark_metrics(recs, 1.0);
    CHECK(m.frames == 5);
    CHECK(m.detected_frames == 3);
    CHECK(m.lost_frames == 2);
    CHECK(m.tracking_frames == 3);
    CHECK_NEAR(m.detection_fraction, 3.0 / 5.0, 1e-15);
}

void test_benchmark_angular_metrics() {  // (20)(21)(22)(23)(24)
    // 5 tracking magnitudes {0.01..0.05} + 1 lost frame (must not change denominators).
    std::vector<TelemetryRecord> recs = {
        tracking_rec(0.01, 0, 0), tracking_rec(0.02, 0, 0), tracking_rec(0.03, 0, 0),
        tracking_rec(0.04, 0, 0), tracking_rec(0.05, 0, 0), lost_rec()};
    const BenchmarkMetrics m = compute_benchmark_metrics(recs, 1.0);

    CHECK_NEAR(m.mean_angular_error_rad, 0.03, 1e-12);                          // (21)
    CHECK_NEAR(m.rms_angular_error_rad, std::sqrt(0.0011), 1e-12);             // (20)
    CHECK_NEAR(m.max_angular_error_rad, 0.05, 1e-15);                          // (22)
    CHECK_NEAR(m.final_angular_error_rad, 0.05, 1e-15);                        // (23)
    CHECK_NEAR(m.p95_angular_error_rad, 0.05, 1e-15);  // N=5 -> ceil(4.75)-1=4 -> 0.05  (24)

    // final != max when the last tracking frame is not the largest.
    std::vector<TelemetryRecord> recs2 = {
        tracking_rec(0.05, 0, 0), tracking_rec(0.02, 0, 0), tracking_rec(0.03, 0, 0)};
    const BenchmarkMetrics m2 = compute_benchmark_metrics(recs2, 1.0);
    CHECK_NEAR(m2.max_angular_error_rad, 0.05, 1e-15);
    CHECK_NEAR(m2.final_angular_error_rad, 0.03, 1e-15);

    // p95 nearest-rank for N=7 -> ceil(6.65)-1 = 6 -> largest.
    std::vector<TelemetryRecord> recs3;
    for (int i = 1; i <= 7; ++i) {
        recs3.push_back(tracking_rec(0.01 * static_cast<double>(i), 0, 0));
    }
    CHECK_NEAR(compute_benchmark_metrics(recs3, 1.0).p95_angular_error_rad, 0.07, 1e-12);
}

void test_benchmark_pixel_metrics() {  // (25)
    // pixel-error magnitudes {5, 0, 10, 13, 0}
    std::vector<TelemetryRecord> recs = {
        tracking_rec(0.01, 3, 4), tracking_rec(0.01, 0, 0), tracking_rec(0.01, 6, 8),
        tracking_rec(0.01, 5, 12), tracking_rec(0.01, 0, 0)};
    const BenchmarkMetrics m = compute_benchmark_metrics(recs, 1.0);
    CHECK_NEAR(m.mean_pixel_error_px, (5.0 + 0.0 + 10.0 + 13.0 + 0.0) / 5.0, 1e-12);
    CHECK_NEAR(m.rms_pixel_error_px, std::sqrt((25.0 + 0.0 + 100.0 + 169.0 + 0.0) / 5.0), 1e-9);
    CHECK_NEAR(m.max_pixel_error_px, 13.0, 1e-12);
}

void test_benchmark_saturation_and_rates() {  // (26)
    std::vector<TelemetryRecord> recs(6);
    for (auto& r : recs) {
        r.tracking_state = TrackingState::Tracking;
        r.angular_error_total_rad = 0.0;
        r.applied_pan_rate_rad_s = 0.2;
        r.applied_tilt_rate_rad_s = -0.1;
    }
    recs[0].pan_saturated = true;
    recs[3].pan_saturated = true;
    recs[3].tilt_saturated = true;
    recs[3].applied_pan_rate_rad_s = 0.5;  // peak

    const BenchmarkMetrics m = compute_benchmark_metrics(recs, 1.0);
    CHECK_NEAR(m.command_saturation_fraction, 2.0 / 6.0, 1e-15);  // records 0 and 3
    CHECK_NEAR(m.pan_saturation_fraction, 2.0 / 6.0, 1e-15);
    CHECK_NEAR(m.tilt_saturation_fraction, 1.0 / 6.0, 1e-15);
    CHECK_NEAR(m.mean_abs_pan_rate_rad_s, (0.2 * 5.0 + 0.5) / 6.0, 1e-12);
    CHECK_NEAR(m.mean_abs_tilt_rate_rad_s, 0.1, 1e-12);
    CHECK_NEAR(m.peak_applied_pan_rate_rad_s, 0.5, 1e-15);
    CHECK_NEAR(m.peak_applied_tilt_rate_rad_s, 0.1, 1e-15);
}

// ---- 27. processing FPS is wall-time-only ------------------

void test_processing_fps_wall_only() {
    std::vector<TelemetryRecord> recs;
    for (int i = 0; i < 100; ++i) {
        recs.push_back(tracking_rec(0.01 + 0.0001 * static_cast<double>(i), 0, 0));
    }
    const BenchmarkMetrics fast = compute_benchmark_metrics(recs, 0.25);
    const BenchmarkMetrics slow = compute_benchmark_metrics(recs, 4.0);

    // processing_fps = frames / wall_execution_time_s, and only that.
    CHECK_NEAR(fast.processing_fps, 100.0 / 0.25, 1e-9);
    CHECK_NEAR(slow.processing_fps, 100.0 / 4.0, 1e-9);
    CHECK(fast.processing_fps != slow.processing_fps);

    // Every error / count metric is identical regardless of the wall time given.
    CHECK(fast.rms_angular_error_rad == slow.rms_angular_error_rad);
    CHECK(fast.mean_angular_error_rad == slow.mean_angular_error_rad);
    CHECK(fast.max_angular_error_rad == slow.max_angular_error_rad);
    CHECK(fast.p95_angular_error_rad == slow.p95_angular_error_rad);
    CHECK(fast.detection_fraction == slow.detection_fraction);
    CHECK(fast.frames == slow.frames);
}

// ---- 28 + mandatory: telemetry does not perturb the simulation ----

void test_telemetry_non_interference() {
    const auto cfg = fsoc::baseline_runner_config();
    fsoc::SinusoidalTrajectory::Parameters p{};
    p.center_position_m = {100.0, 0.0, 3.0};
    p.amplitude_m = {0.0, 22.0, 4.0};
    p.frequency_hz = {0.0, 0.12, 0.09};
    const fsoc::SinusoidalTrajectory target{p};
    fsoc::SimulationRunnerConfig sin_cfg = cfg;
    sin_cfg.initial_tilt_rad = std::atan2(3.0, 100.0);

    // A: plain closed loop, no telemetry.
    fsoc::SimulationRunner runner_a{sin_cfg, target};
    std::vector<SimulationStepResult> a;
    for (int i = 0; i < 600; ++i) {
        a.push_back(runner_a.step());
    }

    // B: same config, telemetry records written interleaved with each step.
    const std::filesystem::path csv = temp_csv("fsoc_step8_noninterference.csv");
    fsoc::SimulationRunner runner_b{sin_cfg, target};
    std::vector<SimulationStepResult> b;
    {
        CsvTelemetryLogger logger{csv};
        logger.write_header();
        for (int i = 0; i < 600; ++i) {
            const SimulationStepResult r = runner_b.step();
            b.push_back(r);
            logger.record(to_rec(r));
        }
        CHECK(logger.records_written() == 600);
    }
    std::filesystem::remove(csv);

    CHECK(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].simulation_time_s == b[i].simulation_time_s);
        CHECK(a[i].frame_index == b[i].frame_index);
        CHECK(a[i].target_truth.position_m.x == b[i].target_truth.position_m.x);
        CHECK(a[i].target_truth.position_m.y == b[i].target_truth.position_m.y);
        CHECK(a[i].target_truth.velocity_mps.z == b[i].target_truth.velocity_mps.z);
        CHECK(a[i].target_visible == b[i].target_visible);
        CHECK(a[i].target_detected == b[i].target_detected);
        CHECK(a[i].camera_pan_rad == b[i].camera_pan_rad);
        CHECK(a[i].camera_tilt_rad == b[i].camera_tilt_rad);
        CHECK(a[i].command.pan_rate_rad_s == b[i].command.pan_rate_rad_s);
        CHECK(a[i].command.tilt_rate_rad_s == b[i].command.tilt_rate_rad_s);
        CHECK(a[i].applied_rates.pan_rate_rad_s == b[i].applied_rates.pan_rate_rad_s);
        CHECK(a[i].applied_rates.tilt_rate_rad_s == b[i].applied_rates.tilt_rate_rad_s);
        CHECK(a[i].detection.has_value() == b[i].detection.has_value());
        if (a[i].detection.has_value() && b[i].detection.has_value()) {
            CHECK(a[i].detection->centroid_px.x_px == b[i].detection->centroid_px.x_px);
            CHECK(a[i].detection->centroid_px.y_px == b[i].detection->centroid_px.y_px);
        }
        CHECK(a[i].tracking_error.has_value() == b[i].tracking_error.has_value());
        if (a[i].tracking_error.has_value() && b[i].tracking_error.has_value()) {
            CHECK(a[i].tracking_error->angular.pan_rad == b[i].tracking_error->angular.pan_rad);
            CHECK(a[i].tracking_error->angular.tilt_rad == b[i].tracking_error->angular.tilt_rad);
        }
    }
}

// ---- benchmark against a real run: values are sane and match evaluate() ----

void test_benchmark_matches_simulation() {
    const auto cfg = fsoc::baseline_runner_config();
    const fsoc::StationaryTrajectory target{fsoc::Vec3{100.0, 6.0, 4.0}};
    fsoc::SimulationRunner runner{cfg, target};
    const fsoc::RecordedRun run = fsoc::run_and_record(runner, 5.0);

    const BenchmarkMetrics bm = compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);
    const fsoc::SimulationMetrics sm = fsoc::evaluate(run.step_results);

    CHECK(bm.frames == sm.frame_count);
    CHECK(bm.detected_frames == sm.detected_frames);
    CHECK(bm.lost_frames == sm.lost_frames);
    CHECK_NEAR(bm.detection_fraction, sm.detection_fraction, 1e-15);
    CHECK_NEAR(bm.rms_angular_error_rad, sm.rms_angular_error_rad, 1e-12);
    CHECK_NEAR(bm.max_angular_error_rad, sm.max_angular_error_rad, 1e-12);
    CHECK_NEAR(bm.final_angular_error_rad, sm.final_angular_error_rad, 1e-12);
    CHECK(bm.processing_fps > 0.0);
    CHECK(bm.wall_execution_time_s > 0.0);
    // Static acquisition converges essentially to zero.
    CHECK(fsoc::rad_to_deg(bm.final_angular_error_rad) < 0.05);
}

// ---- 29..35. prior steps remain green (spot checks) ---------

void test_prior_steps_regression() {
    const fsoc::PanTiltCamera camera{fsoc::CameraConfig{}};
    const auto centre = camera.project({100.0, 0.0, 0.0});
    CHECK(centre.has_value());
    CHECK_NEAR(centre->u_px, camera.cx_px(), 1e-9);

    const fsoc::LinearTrajectory lin{{100.0, 0.0, 2.0}, {0.0, 1.0, 0.2}};
    CHECK_NEAR(lin.state_at(2.0).position_m.z, 2.4, 1e-12);

    CHECK(fsoc::observe_beacon(camera, {-10.0, 0.0, 0.0}).status ==
          fsoc::ObservationStatus::BehindCamera);

    fsoc::PIDController pid{fsoc::PIDControllerConfig{}};
    fsoc::TrackingError e{};
    e.angular.pan_rad = 0.05;
    e.angular.tilt_rad = 0.02;
    const auto cmd = pid.update(e, 0.02);
    CHECK(cmd.pan_rate_rad_s > 0.0 && cmd.tilt_rate_rad_s > 0.0);

    // Step 7 runner still tracks the static target to near-zero error.
    const fsoc::StationaryTrajectory static_target{fsoc::Vec3{100.0, 6.0, 4.0}};
    fsoc::SimulationRunner runner{fsoc::baseline_runner_config(), static_target};
    const auto results = runner.run_for(3.0);
    CHECK(results.back().tracking_error.has_value());
    CHECK(fsoc::rad_to_deg(fsoc::total_angular_error_rad(*results.back().tracking_error)) < 0.05);
}

}  // namespace

int main() {
    test_record_scalar_copies();
    test_record_lost_target_absences();
    test_record_saturation_flags();
    test_csv_header_deterministic();
    test_csv_column_count_and_empty_fields();
    test_csv_multiple_records();
    test_benchmark_counts_and_fraction();
    test_benchmark_angular_metrics();
    test_benchmark_pixel_metrics();
    test_benchmark_saturation_and_rates();
    test_processing_fps_wall_only();
    test_telemetry_non_interference();
    test_benchmark_matches_simulation();
    test_prior_steps_regression();

    if (failures == 0) {
        std::cout << "PASS: 14 Step-8 telemetry checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
