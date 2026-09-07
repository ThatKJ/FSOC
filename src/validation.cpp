#include "fsoc/validation.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <opencv2/core.hpp>

#include "fsoc/config.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/trajectory.hpp"
#include "fsoc/visualization.hpp"

namespace fsoc {

std::string_view to_string(const ValidationScenarioId id) noexcept {
    switch (id) {
        case ValidationScenarioId::StaticAcquisition:
            return "Static Acquisition";
        case ValidationScenarioId::SlowLinearTracking:
            return "Slow Linear Tracking";
        case ValidationScenarioId::SinusoidalTracking:
            return "Sinusoidal Tracking";
        case ValidationScenarioId::NearFovEdgeAcquisition:
            return "Near-FOV-Edge Acquisition";
        case ValidationScenarioId::ActuatorSaturation:
            return "Actuator Saturation";
        case ValidationScenarioId::TargetLossAndReentry:
            return "Target Loss and Re-entry";
        case ValidationScenarioId::OpenLoopComparison:
            return "Open vs Closed Loop";
    }
    return "?";
}

bool evaluate_passed(const ValidationResult& result) {
    return std::all_of(result.checks.begin(), result.checks.end(),
                       [](const AcceptanceCheck& c) { return c.passed; });
}

namespace {

constexpr double kEps = 1e-9;

[[nodiscard]] double deg(const double rad) { return rad_to_deg(rad); }

[[nodiscard]] AcceptanceCheck check(std::string name, const char* comparator, const double actual,
                                    const double limit, std::string unit, const bool passed) {
    return {.name = std::move(name),
            .passed = passed,
            .actual = actual,
            .limit = limit,
            .unit = std::move(unit),
            .comparator = comparator};
}
[[nodiscard]] AcceptanceCheck check_lt(std::string n, double a, double l, std::string u) {
    return check(std::move(n), "<", a, l, std::move(u), a < l);
}
[[nodiscard]] AcceptanceCheck check_le(std::string n, double a, double l, std::string u) {
    return check(std::move(n), "<=", a, l, std::move(u), a <= l + kEps);
}
[[nodiscard]] AcceptanceCheck check_gt(std::string n, double a, double l, std::string u) {
    return check(std::move(n), ">", a, l, std::move(u), a > l);
}
[[nodiscard]] AcceptanceCheck check_ge(std::string n, double a, double l, std::string u) {
    return check(std::move(n), ">=", a, l, std::move(u), a >= l - kEps);
}
[[nodiscard]] AcceptanceCheck check_bool(std::string n, const bool ok) {
    return check(std::move(n), "bool", ok ? 1.0 : 0.0, 1.0, "", ok);
}

// ---- run helpers ------------------------------------------------

[[nodiscard]] RecordedRun run_recorded(
    const SimulationRunnerConfig& config,
    const Trajectory& trajectory,
    const double duration_s) {
    SimulationRunner runner{config, trajectory};
    return run_and_record(runner, duration_s);
}

[[nodiscard]] bool runs_identical(const RecordedRun& a, const RecordedRun& b) {
    if (a.step_results.size() != b.step_results.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.step_results.size(); ++i) {
        const SimulationStepResult& x = a.step_results[i];
        const SimulationStepResult& y = b.step_results[i];
        if (x.simulation_time_s != y.simulation_time_s) return false;
        if (x.camera_pan_rad != y.camera_pan_rad) return false;
        if (x.camera_tilt_rad != y.camera_tilt_rad) return false;
        if (x.command.pan_rate_rad_s != y.command.pan_rate_rad_s) return false;
        if (x.command.tilt_rate_rad_s != y.command.tilt_rate_rad_s) return false;
        if (x.applied_rates.pan_rate_rad_s != y.applied_rates.pan_rate_rad_s) return false;
        if (x.detection.has_value() != y.detection.has_value()) return false;
        if (x.detection.has_value() &&
            (x.detection->centroid_px.x_px != y.detection->centroid_px.x_px ||
             x.detection->centroid_px.y_px != y.detection->centroid_px.y_px)) {
            return false;
        }
        if (x.tracking_error.has_value() != y.tracking_error.has_value()) return false;
    }
    return true;
}

[[nodiscard]] bool metrics_identical(const BenchmarkMetrics& a, const BenchmarkMetrics& b) {
    return a.frames == b.frames && a.detected_frames == b.detected_frames &&
           a.lost_frames == b.lost_frames && a.tracking_frames == b.tracking_frames &&
           a.detection_fraction == b.detection_fraction &&
           a.rms_angular_error_rad == b.rms_angular_error_rad &&
           a.mean_angular_error_rad == b.mean_angular_error_rad &&
           a.max_angular_error_rad == b.max_angular_error_rad &&
           a.final_angular_error_rad == b.final_angular_error_rad &&
           a.p95_angular_error_rad == b.p95_angular_error_rad &&
           a.rms_pixel_error_px == b.rms_pixel_error_px &&
           a.command_saturation_fraction == b.command_saturation_fraction;
}

// ---- telemetry inspectors ----------------------------------------

[[nodiscard]] bool all_finite(const std::vector<TelemetryRecord>& tel) {
    auto ok = [](const std::optional<double>& v) { return !v.has_value() || std::isfinite(*v); };
    for (const TelemetryRecord& t : tel) {
        if (!std::isfinite(t.simulation_time_s) || !std::isfinite(t.camera_pan_rad) ||
            !std::isfinite(t.camera_tilt_rad) || !std::isfinite(t.command_pan_rate_rad_s) ||
            !std::isfinite(t.command_tilt_rate_rad_s) ||
            !std::isfinite(t.applied_pan_rate_rad_s) ||
            !std::isfinite(t.applied_tilt_rate_rad_s) ||
            !std::isfinite(t.target_position_x_m) || !std::isfinite(t.target_position_y_m) ||
            !std::isfinite(t.target_position_z_m)) {
            return false;
        }
        if (!ok(t.detected_x_px) || !ok(t.detected_y_px) || !ok(t.pixel_error_x_px) ||
            !ok(t.pixel_error_y_px) || !ok(t.angular_error_pan_rad) ||
            !ok(t.angular_error_tilt_rad) || !ok(t.angular_error_total_rad) ||
            !ok(t.detection_error_px)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] double max_dt_deviation(const std::vector<TelemetryRecord>& tel, const double dt) {
    double worst = 0.0;
    for (std::size_t i = 0; i < tel.size(); ++i) {
        worst = std::max(worst,
                         std::abs(tel[i].simulation_time_s - static_cast<double>(i) * dt));
    }
    return worst;
}

[[nodiscard]] bool timestamps_monotonic(const std::vector<TelemetryRecord>& tel) {
    for (std::size_t i = 1; i < tel.size(); ++i) {
        if (!(tel[i].simulation_time_s > tel[i - 1].simulation_time_s)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] double max_abs_command(const std::vector<TelemetryRecord>& tel) {
    double m = 0.0;
    for (const TelemetryRecord& t : tel) {
        m = std::max({m, std::abs(t.command_pan_rate_rad_s), std::abs(t.command_tilt_rate_rad_s)});
    }
    return m;
}

[[nodiscard]] double max_abs_applied(const std::vector<TelemetryRecord>& tel) {
    double m = 0.0;
    for (const TelemetryRecord& t : tel) {
        m = std::max({m, std::abs(t.applied_pan_rate_rad_s), std::abs(t.applied_tilt_rate_rad_s)});
    }
    return m;
}

// Every lost frame must obey the frozen target-loss contract.
[[nodiscard]] bool loss_semantics_ok(const std::vector<SimulationStepResult>& res,
                                     const std::vector<TelemetryRecord>& tel) {
    for (std::size_t i = 0; i < res.size(); ++i) {
        if (res[i].detection.has_value()) {
            continue;
        }
        if (res[i].command.pan_rate_rad_s != 0.0 || res[i].command.tilt_rate_rad_s != 0.0) {
            return false;
        }
        if (res[i].tracking_error.has_value()) return false;
        if (tel[i].tracking_state != TrackingState::TargetLost) return false;
        if (tel[i].detected_x_px.has_value() || tel[i].pixel_error_x_px.has_value() ||
            tel[i].angular_error_pan_rad.has_value()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] double angular_total_deg(const TelemetryRecord& t) {
    return t.angular_error_total_rad.has_value() ? deg(*t.angular_error_total_rad)
                                                : std::nan("");
}

[[nodiscard]] double last_pixel_offset_px(const std::vector<TelemetryRecord>& tel) {
    for (std::size_t k = tel.size(); k-- > 0;) {
        if (tel[k].pixel_error_x_px.has_value() && tel[k].pixel_error_y_px.has_value()) {
            return std::hypot(*tel[k].pixel_error_x_px, *tel[k].pixel_error_y_px);
        }
    }
    return std::nan("");
}

[[nodiscard]] double window_rms_deg(const std::vector<TelemetryRecord>& tel, const std::size_t a,
                                    const std::size_t b) {
    double s = 0.0;
    std::size_t n = 0;
    for (std::size_t i = a; i < b && i < tel.size(); ++i) {
        if (tel[i].angular_error_total_rad.has_value()) {
            const double e = deg(*tel[i].angular_error_total_rad);
            s += e * e;
            ++n;
        }
    }
    return n > 0 ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}

[[nodiscard]] std::size_t count_saturated(const std::vector<TelemetryRecord>& tel) {
    return static_cast<std::size_t>(std::count_if(tel.begin(), tel.end(), [](const TelemetryRecord& t) {
        return t.pan_saturated || t.tilt_saturated;
    }));
}

// ---- evidence writers -----------------------------------------

void write_scenario_csv(
    const std::filesystem::path& dir,
    const std::string& stem,
    const std::vector<TelemetryRecord>& tel,
    ValidationResult& out) {
    if (dir.empty()) {
        return;
    }
    const std::filesystem::path path = dir / (stem + ".csv");
    CsvTelemetryLogger logger{path};
    logger.write_header();
    for (const TelemetryRecord& t : tel) {
        logger.record(t);
    }
    out.csv_path = path;
}

void write_evidence_frames(
    const std::filesystem::path& dir,
    const std::string& stem,
    const RecordedRun& run,
    const std::vector<std::pair<std::size_t, std::string>>& frames,
    ValidationResult& out) {
    if (dir.empty()) {
        return;
    }
    const TrackingVisualizer visualizer{VisualizationConfig{}};  // observer-only, defaults
    for (const auto& [index, tag] : frames) {
        if (index >= run.step_results.size()) {
            continue;
        }
        // The EXACT frame the detector consumed this step (stored on the
        // result) -- not a re-render from `observation`. A fresh re-render is
        // only guaranteed identical while rendering has no noise/disturbance
        // sources; using the stored frame keeps evidence screenshots honest
        // once that stops being true.
        const cv::Mat& base = run.step_results[index].rendered_frame;
        const cv::Mat annotated =
            visualizer.annotate(base, run.step_results[index], run.telemetry[index]);
        const std::filesystem::path path = dir / (stem + "_" + tag + ".png");
        if (write_png(path, annotated)) {
            out.evidence_images.push_back(path);
        }
    }
}

// ---- report -------------------------------------------------

[[nodiscard]] std::string now_string() {
    const std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

[[nodiscard]] std::string fmt(const double v, const int precision = 4) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(precision);
    os << v;
    return os.str();
}

}  // namespace

// ===========================================================================
// global_acceptance_checks
// ===========================================================================

std::vector<AcceptanceCheck> global_acceptance_checks(
    const std::vector<SimulationStepResult>& results,
    const std::vector<TelemetryRecord>& telemetry,
    const SimulationRunnerConfig& config) {
    std::vector<AcceptanceCheck> checks;
    const double pid_limit =
        std::min(config.controller.pan.output_limit_rad_s, config.controller.tilt.output_limit_rad_s);
    const double actuator_limit =
        std::min(config.camera.max_pan_rate_rad_s, config.camera.max_tilt_rate_rad_s);

    checks.push_back(check_bool("frame count > 0", !results.empty()));
    checks.push_back(check_bool("simulation timestamps monotonic", timestamps_monotonic(telemetry)));
    checks.push_back(check_le("fixed-dt deviation", max_dt_deviation(telemetry, config.timestep_s),
                              1e-9, "s"));
    checks.push_back(check_bool("all telemetry values finite (no NaN/Inf)", all_finite(telemetry)));
    checks.push_back(check_le("max command rate", max_abs_command(telemetry), pid_limit, "rad/s"));
    checks.push_back(
        check_le("max applied rate", max_abs_applied(telemetry), actuator_limit, "rad/s"));
    checks.push_back(
        check_bool("target-loss semantics (zero cmd / no error / TargetLost)",
                   loss_semantics_ok(results, telemetry)));
    return checks;
}

// ===========================================================================
// ValidationSuite
// ===========================================================================

ValidationSuite::ValidationSuite(std::filesystem::path evidence_dir)
    : evidence_dir_(std::move(evidence_dir)) {
    if (!evidence_dir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(evidence_dir_, ec);
    }
}

namespace {

// Shared tail: append global + determinism checks, finalise `passed`.
void finalise(
    ValidationResult& r,
    const RecordedRun& run,
    const RecordedRun& replay,
    const SimulationRunnerConfig& config) {
    const auto global = global_acceptance_checks(run.step_results, run.telemetry, config);
    r.checks.insert(r.checks.end(), global.begin(), global.end());

    const BenchmarkMetrics replay_metrics =
        compute_benchmark_metrics(replay.telemetry, replay.wall_execution_time_s);
    r.deterministic = runs_identical(run, replay) && metrics_identical(r.metrics, replay_metrics);
    r.checks.push_back(check_bool("deterministic replay (identical metrics + step results)",
                                  r.deterministic));

    r.passed = evaluate_passed(r);
}

}  // namespace

ValidationResult ValidationSuite::run_static_acquisition() {
    ValidationResult r{};
    r.id = ValidationScenarioId::StaticAcquisition;
    r.scenario_name = std::string{to_string(r.id)};
    r.description =
        "Coarse alignment from an initial pointing offset. Stationary target {100, 6, 4} m; "
        "camera starts pan=0 tilt=0; 4 s.";

    const SimulationRunnerConfig cfg = baseline_runner_config();
    const StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};
    const RecordedRun run = run_recorded(cfg, target, 4.0);
    const RecordedRun replay = run_recorded(cfg, target, 4.0);
    r.metrics = compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);

    const auto& tel = run.telemetry;
    const double initial_deg = angular_total_deg(tel.front());
    const double final_deg = deg(r.metrics.final_angular_error_rad);
    const double ratio = initial_deg > 0.0 ? final_deg / initial_deg : 1.0;

    r.checks.push_back(check_bool("target detected in first frame", tel.front().target_detected));
    r.checks.push_back(check_ge("detection fraction", 100.0 * r.metrics.detection_fraction, 100.0, "%"));
    r.checks.push_back(check_gt("initial total angular error", initial_deg, 2.0, "deg"));
    r.checks.push_back(check_lt("final total angular error", final_deg, 0.05, "deg"));
    r.checks.push_back(check_lt("final centroid offset", last_pixel_offset_px(tel), 2.0, "px"));
    r.checks.push_back(check_lt("final / initial error ratio", ratio, 0.02, ""));
    r.checks.push_back(
        check_bool("system remains TRACKING", tel.back().tracking_state == TrackingState::Tracking));

    write_scenario_csv(evidence_dir_, "static", tel, r);
    write_evidence_frames(evidence_dir_, "static", run,
                          {{0, "initial"}, {tel.size() / 2, "mid"}, {tel.size() - 1, "final"}}, r);
    finalise(r, run, replay, cfg);
    return r;
}

ValidationResult ValidationSuite::run_slow_linear() {
    ValidationResult r{};
    r.id = ValidationScenarioId::SlowLinearTracking;
    r.scenario_name = std::string{to_string(r.id)};
    r.description =
        "Continual tracking of a mobile terminal. Linear target start {100, -8, -3} m, "
        "velocity {0, 2.0, 0.8} m/s; 10 s.";

    const SimulationRunnerConfig cfg = baseline_runner_config();
    const LinearTrajectory target{Vec3{100.0, -8.0, -3.0}, Vec3{0.0, 2.0, 0.8}};
    const RecordedRun run = run_recorded(cfg, target, 10.0);
    const RecordedRun replay = run_recorded(cfg, target, 10.0);
    r.metrics = compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);

    r.checks.push_back(check_ge("detection fraction", 100.0 * r.metrics.detection_fraction, 95.0, "%"));
    r.checks.push_back(check_lt("RMS angular error", deg(r.metrics.rms_angular_error_rad), 0.75, "deg"));
    r.checks.push_back(check_lt("P95 angular error", deg(r.metrics.p95_angular_error_rad), 1.0, "deg"));
    r.checks.push_back(
        check_lt("final angular error", deg(r.metrics.final_angular_error_rad), 0.30, "deg"));
    r.checks.push_back(check_le("lost frames", static_cast<double>(r.metrics.lost_frames), 0.0, "frames"));

    write_scenario_csv(evidence_dir_, "linear", run.telemetry, r);
    write_evidence_frames(evidence_dir_, "linear", run,
                          {{0, "initial"}, {run.telemetry.size() / 2, "mid"},
                           {run.telemetry.size() - 1, "final"}},
                          r);
    finalise(r, run, replay, cfg);
    return r;
}

ValidationResult ValidationSuite::run_sinusoidal() {
    ValidationResult r{};
    r.id = ValidationScenarioId::SinusoidalTracking;
    r.scenario_name = std::string{to_string(r.id)};
    r.description =
        "Tracking a nonlinear moving target. Sinusoid centre {100,0,3}, amplitude {0,22,4} m, "
        "frequency {0,0.12,0.09} Hz (+/-12.4 deg Y swing); 20 s.";

    SimulationRunnerConfig cfg = baseline_runner_config();
    cfg.initial_tilt_rad = std::atan2(3.0, 100.0);
    SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 22.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.12, 0.09};
    const SinusoidalTrajectory target{p};

    const RecordedRun run = run_recorded(cfg, target, 20.0);
    const RecordedRun replay = run_recorded(cfg, target, 20.0);
    r.metrics = compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);

    const double early_rms = window_rms_deg(run.telemetry, 100, 350);
    const double late_rms = window_rms_deg(run.telemetry, 750, run.telemetry.size());
    const double growth = early_rms > 0.0 ? late_rms / early_rms : 0.0;

    r.checks.push_back(check_ge("detection fraction", 100.0 * r.metrics.detection_fraction, 95.0, "%"));
    r.checks.push_back(check_lt("RMS angular error", deg(r.metrics.rms_angular_error_rad), 1.0, "deg"));
    r.checks.push_back(check_lt("P95 angular error", deg(r.metrics.p95_angular_error_rad), 1.0, "deg"));
    r.checks.push_back(check_lt("max angular error", deg(r.metrics.max_angular_error_rad), 1.5, "deg"));
    r.checks.push_back(check_le("lost frames", static_cast<double>(r.metrics.lost_frames), 0.0, "frames"));
    r.checks.push_back(check_le("late/early RMS growth", growth, 1.5, ""));

    write_scenario_csv(evidence_dir_, "sinusoidal", run.telemetry, r);
    const std::size_t n = run.telemetry.size();
    write_evidence_frames(evidence_dir_, "sinusoidal", run,
                          {{0, "t00"}, {n / 4, "t05"}, {n / 2, "t10"}, {3 * n / 4, "t15"}}, r);
    finalise(r, run, replay, cfg);
    return r;
}

ValidationResult ValidationSuite::run_near_fov_edge() {
    ValidationResult r{};
    r.id = ValidationScenarioId::NearFovEdgeAcquisition;
    r.scenario_name = std::string{to_string(r.id)};
    r.description =
        "Acquisition near the operational FOV boundary. Stationary target {100, 15.5, 8} m "
        "(bearing ~8.8 deg pan / ~4.5 deg tilt; ~88% / ~60% of the half-FOV); camera pan=0 tilt=0; 4 s.";

    const SimulationRunnerConfig cfg = baseline_runner_config();
    const StationaryTrajectory target{Vec3{100.0, 15.5, 8.0}};
    const RecordedRun run = run_recorded(cfg, target, 4.0);
    const RecordedRun replay = run_recorded(cfg, target, 4.0);
    r.metrics = compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);

    const auto& tel = run.telemetry;
    const TelemetryRecord& f0 = tel.front();
    const bool direction_ok = f0.pixel_error_x_px.value_or(0.0) > 0.0 &&
                              f0.pixel_error_y_px.value_or(0.0) < 0.0 &&
                              f0.command_pan_rate_rad_s > 0.0 && f0.command_tilt_rate_rad_s > 0.0;
    bool no_immediate_loss = true;
    for (std::size_t i = 0; i < 25 && i < tel.size(); ++i) {
        no_immediate_loss = no_immediate_loss && tel[i].target_detected;
    }
    const double initial_deg = angular_total_deg(f0);
    const double final_deg = deg(r.metrics.final_angular_error_rad);
    const double ratio = initial_deg > 0.0 ? final_deg / initial_deg : 1.0;

    r.checks.push_back(check_bool("target initially visible (in FOV)", f0.target_visible));
    r.checks.push_back(check_bool("target initially detected", f0.target_detected));
    r.checks.push_back(check_bool("command points toward target (RIGHT + ABOVE)", direction_ok));
    r.checks.push_back(check_bool("no immediate target loss (first 0.5 s detected)", no_immediate_loss));
    r.checks.push_back(check_lt("final / initial error ratio (converges)", ratio, 0.05, ""));
    r.checks.push_back(check_lt("final angular error", final_deg, 0.10, "deg"));
    r.checks.push_back(check_lt("final centroid offset", last_pixel_offset_px(tel), 3.0, "px"));

    write_scenario_csv(evidence_dir_, "fov_edge", tel, r);
    write_evidence_frames(evidence_dir_, "fov_edge", run,
                          {{0, "initial"}, {tel.size() - 1, "final"}}, r);
    finalise(r, run, replay, cfg);
    return r;
}

ValidationResult ValidationSuite::run_actuator_saturation() {
    ValidationResult r{};
    r.id = ValidationScenarioId::ActuatorSaturation;
    r.scenario_name = std::string{to_string(r.id)};
    r.description =
        "Correct behaviour WHILE the actuator is rate-limited. Stationary target {100, 16, 12} m "
        "(~11 deg initial error -> PID demands > the actuator rate for several frames); 4 s.";

    const SimulationRunnerConfig cfg = baseline_runner_config();
    const StationaryTrajectory target{Vec3{100.0, 16.0, 12.0}};
    const RecordedRun run = run_recorded(cfg, target, 4.0);
    const RecordedRun replay = run_recorded(cfg, target, 4.0);
    r.metrics = compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);

    const auto& tel = run.telemetry;
    const auto& res = run.step_results;
    const std::size_t sat_frames = count_saturated(tel);
    const double pid_limit = cfg.controller.pan.output_limit_rad_s;
    const double actuator_limit = cfg.camera.max_pan_rate_rad_s;
    const bool ends_unsaturated = !tel.back().pan_saturated && !tel.back().tilt_saturated;
    const bool error_reduced =
        angular_total_deg(tel[tel.size() / 4]) < angular_total_deg(tel.front()) &&
        angular_total_deg(tel[tel.size() / 2]) < angular_total_deg(tel[tel.size() / 4]);
    const bool any_flag = std::any_of(tel.begin(), tel.end(), [](const TelemetryRecord& t) {
        return t.pan_saturated || t.tilt_saturated;
    });
    (void)res;

    r.checks.push_back(check_gt("saturated frames occur", static_cast<double>(sat_frames), 2.0, "frames"));
    r.checks.push_back(check_le("max command rate", max_abs_command(tel), pid_limit, "rad/s"));
    r.checks.push_back(check_le("max applied rate", max_abs_applied(tel), actuator_limit, "rad/s"));
    r.checks.push_back(check_bool("saturation flags present in telemetry", any_flag));
    r.checks.push_back(check_bool("system comes off the limit (final frame not saturated)",
                                  ends_unsaturated));
    r.checks.push_back(check_bool("tracking error reduced during and after saturation", error_reduced));
    r.checks.push_back(
        check_lt("final angular error", deg(r.metrics.final_angular_error_rad), 0.05, "deg"));

    write_scenario_csv(evidence_dir_, "saturation", tel, r);
    write_evidence_frames(evidence_dir_, "saturation", run,
                          {{0, "initial"}, {tel.size() - 1, "final"}}, r);
    finalise(r, run, replay, cfg);
    return r;
}

ValidationResult ValidationSuite::run_loss_and_reentry() {
    ValidationResult r{};
    r.id = ValidationScenarioId::TargetLossAndReentry;
    r.scenario_name = std::string{to_string(r.id)};
    r.description =
        "Explicit lost-target semantics and safe recovery. Aggressive sinusoid amplitude "
        "{0,42,4} m, frequency {0,0.30,0.10} Hz: TRACKING -> leaves FOV -> TARGET LOST -> "
        "naturally returns -> TRACKING. Baseline loss policy: PID reset, zero command, camera holds. 8 s.";

    SimulationRunnerConfig cfg = baseline_runner_config();
    cfg.initial_tilt_rad = std::atan2(3.0, 100.0);
    SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 42.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.30, 0.10};
    const SinusoidalTrajectory target{p};

    const RecordedRun run = run_recorded(cfg, target, 8.0);
    const RecordedRun replay = run_recorded(cfg, target, 8.0);
    r.metrics = compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);

    const auto& res = run.step_results;
    const auto& tel = run.telemetry;

    std::size_t first_lost = res.size();
    bool track_to_lost = false;
    bool lost_to_track = false;
    std::size_t reacq = res.size();
    for (std::size_t i = 1; i < res.size(); ++i) {
        const bool prev = res[i - 1].detection.has_value();
        const bool cur = res[i].detection.has_value();
        if (prev && !cur) {
            track_to_lost = true;
            if (first_lost == res.size()) {
                first_lost = i;
            }
        }
        if (!prev && cur) {
            lost_to_track = true;
            if (reacq == res.size() && first_lost != res.size()) {
                reacq = i;
            }
        }
    }

    bool pose_held = true;
    for (std::size_t i = 1; i < res.size(); ++i) {
        if (!res[i].detection.has_value() && !res[i - 1].detection.has_value()) {
            pose_held = pose_held && res[i].camera_pan_rad == res[i - 1].camera_pan_rad &&
                        res[i].camera_tilt_rad == res[i - 1].camera_tilt_rad;
        }
    }

    bool resumes = false;
    if (reacq < res.size()) {
        const bool has_error = res[reacq].tracking_error.has_value();
        const std::size_t later = reacq + 40;
        const bool decreased = later < tel.size() &&
                               angular_total_deg(tel[later]) < angular_total_deg(tel[reacq]);
        resumes = has_error && decreased;
    }

    r.checks.push_back(check_gt("lost frames occur", static_cast<double>(r.metrics.lost_frames), 0.0,
                                "frames"));
    r.checks.push_back(check_bool("TRACKING -> TARGET LOST transition", track_to_lost));
    r.checks.push_back(check_bool("TARGET LOST -> TRACKING transition (natural reacquire)", lost_to_track));
    r.checks.push_back(check_bool("command exactly zero while lost", loss_semantics_ok(res, tel)));
    r.checks.push_back(check_bool("camera pose held across consecutive lost frames", pose_held));
    r.checks.push_back(check_bool("first post-reacquisition control resumes (error decreases)", resumes));

    write_scenario_csv(evidence_dir_, "loss_reentry", tel, r);
    const std::size_t before = first_lost > 5 ? first_lost - 5 : 0;
    write_evidence_frames(evidence_dir_, "loss", run,
                          {{before, "before"},
                           {std::min(first_lost, res.size() - 1), "lost"},
                           {std::min(reacq, res.size() - 1), "reacquired"}},
                          r);
    finalise(r, run, replay, cfg);
    return r;
}

ValidationResult ValidationSuite::run_open_vs_closed() {
    ValidationResult r{};
    r.id = ValidationScenarioId::OpenLoopComparison;
    r.scenario_name = std::string{to_string(r.id)};
    r.description =
        "Same nominal sinusoid (amplitude {0,22,4} m, frequency {0,0.12,0.09} Hz), run open-loop "
        "(camera fixed) then closed-loop; 20 s each.";
    r.has_open_loop_comparison = true;

    SimulationRunnerConfig base = baseline_runner_config();
    base.initial_tilt_rad = std::atan2(3.0, 100.0);
    SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 22.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.12, 0.09};
    const SinusoidalTrajectory target{p};

    SimulationRunnerConfig open_cfg = base;
    open_cfg.control_enabled = false;
    const RecordedRun open_run = run_recorded(open_cfg, target, 20.0);
    r.open_loop_metrics = compute_benchmark_metrics(open_run.telemetry, open_run.wall_execution_time_s);

    const RecordedRun closed_run = run_recorded(base, target, 20.0);
    const RecordedRun closed_replay = run_recorded(base, target, 20.0);
    r.metrics = compute_benchmark_metrics(closed_run.telemetry, closed_run.wall_execution_time_s);

    const BenchmarkMetrics& o = r.open_loop_metrics;
    const BenchmarkMetrics& c = r.metrics;
    const double rms_improvement = c.rms_angular_error_rad > 0.0
                                       ? o.rms_angular_error_rad / c.rms_angular_error_rad
                                       : 0.0;
    const double detect_improvement_pts = 100.0 * (c.detection_fraction - o.detection_fraction);

    r.checks.push_back(check_gt("closed detection > open detection", 100.0 * c.detection_fraction,
                                100.0 * o.detection_fraction, "%"));
    r.checks.push_back(check_lt("closed RMS < open RMS", deg(c.rms_angular_error_rad),
                                deg(o.rms_angular_error_rad), "deg"));
    r.checks.push_back(check_lt("closed P95 < open P95", deg(c.p95_angular_error_rad),
                                deg(o.p95_angular_error_rad), "deg"));
    r.checks.push_back(check_lt("closed lost frames < open lost frames",
                                static_cast<double>(c.lost_frames),
                                static_cast<double>(o.lost_frames), "frames"));
    r.checks.push_back(check_ge("RMS improvement factor", rms_improvement, 3.0, "x"));
    r.checks.push_back(check_ge("detection improvement", detect_improvement_pts, 20.0, "pts"));

    write_scenario_csv(evidence_dir_, "closed_loop", closed_run.telemetry, r);
    write_scenario_csv(evidence_dir_, "open_loop", open_run.telemetry, r);  // overwrites csv_path
    r.csv_path = evidence_dir_.empty() ? std::filesystem::path{}
                                       : evidence_dir_ / "closed_loop.csv";
    write_evidence_frames(evidence_dir_, "open_vs_closed", closed_run,
                          {{0, "t00"}, {closed_run.telemetry.size() / 2, "t10"}}, r);

    finalise(r, closed_run, closed_replay, base);
    return r;
}

ValidationSuiteResult ValidationSuite::run_all() {
    ValidationSuiteResult suite{};
    suite.scenarios.push_back(run_static_acquisition());
    suite.scenarios.push_back(run_slow_linear());
    suite.scenarios.push_back(run_sinusoidal());
    suite.scenarios.push_back(run_near_fov_edge());
    suite.scenarios.push_back(run_actuator_saturation());
    suite.scenarios.push_back(run_loss_and_reentry());
    suite.scenarios.push_back(run_open_vs_closed());
    suite.overall_passed = std::all_of(suite.scenarios.begin(), suite.scenarios.end(),
                                       [](const ValidationResult& s) { return s.passed; });
    return suite;
}

bool ValidationSuite::write_report(const ValidationSuiteResult& result) const {
    if (evidence_dir_.empty()) {
        return false;
    }
    const std::filesystem::path path = evidence_dir_ / "VALIDATION_REPORT.md";
    std::ofstream out{path, std::ios::out | std::ios::trunc};
    if (!out.is_open()) {
        return false;
    }

    const SimulationRunnerConfig base = baseline_runner_config();
    const CameraConfig& cam = base.camera;

    out << "# SIH26169 FSOC — Baseline (v1_baseline candidate) Validation Report\n\n"
        << "Generated: " << now_string() << "  (generated evidence — not canonical design doc)\n\n"
        << "## Configuration\n\n"
        << "| item | value |\n|---|---|\n"
        << "| simulation rate | " << fmt(1.0 / base.timestep_s, 1) << " Hz (fixed dt = "
        << fmt(base.timestep_s, 3) << " s) |\n"
        << "| PID gains (both axes) | kp = " << fmt(base.controller.pan.kp, 1)
        << ", ki = " << fmt(base.controller.pan.ki, 1)
        << ", kd = " << fmt(base.controller.pan.kd, 2) << " |\n"
        << "| PID output limit | " << fmt(deg(base.controller.pan.output_limit_rad_s), 1)
        << " deg/s |\n"
        << "| image size | " << cam.width_px << " x " << cam.height_px << " px |\n"
        << "| FOV | " << fmt(deg(cam.hfov_rad), 1) << " deg H x " << fmt(deg(cam.vfov_rad), 1)
        << " deg V |\n"
        << "| actuator rate limit | " << fmt(deg(cam.max_pan_rate_rad_s), 1)
        << " deg/s pan / " << fmt(deg(cam.max_tilt_rate_rad_s), 1) << " deg/s tilt |\n"
        << "| tilt mechanical stops | " << fmt(deg(cam.min_tilt_rad), 1) << " .. "
        << fmt(deg(cam.max_tilt_rad), 1) << " deg |\n\n";

    out << "## Scenario results\n\n";
    for (const ValidationResult& s : result.scenarios) {
        out << "### " << s.scenario_name << " — " << (s.passed ? "PASS" : "**FAIL**") << "\n\n"
            << s.description << "\n\n"
            << "Metrics: detection " << fmt(100.0 * s.metrics.detection_fraction, 1) << " %"
            << ", RMS " << fmt(deg(s.metrics.rms_angular_error_rad), 4) << " deg"
            << ", P95 " << fmt(deg(s.metrics.p95_angular_error_rad), 4) << " deg"
            << ", max " << fmt(deg(s.metrics.max_angular_error_rad), 4) << " deg"
            << ", final " << fmt(deg(s.metrics.final_angular_error_rad), 4) << " deg"
            << ", lost " << s.metrics.lost_frames << " / " << s.metrics.frames << " frames"
            << ", RMS pixel error " << fmt(s.metrics.rms_pixel_error_px, 2) << " px"
            << ", deterministic replay " << (s.deterministic ? "YES" : "NO") << ".\n\n";
        if (s.has_open_loop_comparison) {
            const BenchmarkMetrics& o = s.open_loop_metrics;
            out << "Open-loop: detection " << fmt(100.0 * o.detection_fraction, 1) << " %, RMS "
                << fmt(deg(o.rms_angular_error_rad), 4) << " deg, P95 "
                << fmt(deg(o.p95_angular_error_rad), 4) << " deg, lost " << o.lost_frames
                << " frames.\n\n";
        }
        out << "| check | actual | | limit | unit | result |\n|---|---:|:-:|---:|---|:-:|\n";
        for (const AcceptanceCheck& c : s.checks) {
            out << "| " << c.name << " | " << fmt(c.actual, 4) << " | " << c.comparator << " | "
                << fmt(c.limit, 4) << " | " << c.unit << " | " << (c.passed ? "PASS" : "FAIL")
                << " |\n";
        }
        out << "\nCSV: `" << s.csv_path.string() << "`";
        if (s.id == ValidationScenarioId::OpenLoopComparison && !evidence_dir_.empty()) {
            out << "  (+ `" << (evidence_dir_ / "open_loop.csv").string() << "`)";
        }
        out << "\n\nEvidence frames:\n";
        for (const auto& img : s.evidence_images) {
            out << "- `" << img.string() << "`\n";
        }
        out << "\n";
    }

    out << "## Overall verdict\n\n**BASELINE ACCEPTANCE: "
        << (result.overall_passed ? "PASS" : "FAIL") << "**\n";
    out.flush();
    return true;
}

}  // namespace fsoc
