// fsoc_demo — human-friendly entry point for the frozen v1_baseline engine.
//
//   ./fsoc_demo static
//   ./fsoc_demo sinusoidal --csv generated/demo/sinusoidal.csv
//   ./fsoc_demo loss --duration 6
//   ./fsoc_demo open        ./fsoc_demo closed        ./fsoc_demo --help
//
// It only packages the validated system: DemoSession owns a SimulationRunner and
// the Step-8 telemetry conversion; this file adds no physics, no control, no
// networking. Core values are radians; the CLI prints degrees for humans.

#include <chrono>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/config.hpp"
#include "fsoc/demo.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/telemetry.hpp"

namespace {

using namespace fsoc;

int usage_error(const std::string& message) {
    std::cerr << "fsoc_demo: " << message << "\n\n" << demo_help_text() << '\n';
    return 2;
}

// "classical" (default) | "ai" | "hybrid" -> PerceptionMode. std::nullopt for
// anything else, so the CLI turns a typo into a clean usage error.
std::optional<PerceptionMode> parse_perception_mode(const std::string& token) {
    if (token == "classical") return PerceptionMode::Classical;
    if (token == "ai") return PerceptionMode::AI;
    if (token == "hybrid") return PerceptionMode::Hybrid;
    return std::nullopt;
}

// models/tiny_beacon_net.onnx resolved relative to the current working
// directory, matching every other fsoc app's path convention (ai_inference_benchmark,
// generate_ai_dataset): run fsoc_demo from the project root.
AiBeaconDetectorConfig default_ai_detector_config() {
    AiBeaconDetectorConfig config{};
    config.model_path = "models/tiny_beacon_net.onnx";
    config.presence_threshold = 0.95;  // frozen, models/threshold.json (do not retune here)
    return config;
}

// Fixed-width degree string for the running status lines.
std::string deg(const double radians, const int precision, const int width) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << std::setw(width) << rad_to_deg(radians);
    return os.str();
}

void print_status_line(
    const DemoSnapshot& s, const TelemetryRecord& t, const bool show_perception, const bool show_tracker) {
    std::cout << "t=" << std::fixed << std::setprecision(2) << std::setw(6) << s.simulation_time_s
              << "s  " << std::left << std::setw(11) << to_string(s.state) << std::right
              << "  tgt=(" << std::setprecision(1) << std::setw(7) << s.target.x_m << ","
              << std::setw(7) << s.target.y_m << "," << std::setw(6) << s.target.z_m << ")"
              << "  cam p/t=" << deg(s.camera.pan_rad, 2, 7) << "/" << deg(s.camera.tilt_rad, 2, 6)
              << " deg";

    if (s.detection.detected && s.detection.x_px.has_value() && s.detection.y_px.has_value()) {
        std::cout << "  det=(" << std::setprecision(1) << std::setw(6) << *s.detection.x_px << ","
                  << std::setw(6) << *s.detection.y_px << ")";
    } else {
        std::cout << "  det=(   --  ,   --  )";
    }

    if (s.tracking.total_error_rad.has_value()) {
        std::cout << "  angErr=" << deg(*s.tracking.total_error_rad, 4, 8) << " deg";
    } else {
        std::cout << "  angErr=      -- ";
    }

    std::cout << "  cmd p/t=" << deg(s.control.command_pan_rate_rad_s, 2, 7) << "/"
              << deg(s.control.command_tilt_rate_rad_s, 2, 6) << " deg/s";
    if (s.control.pan_saturated || s.control.tilt_saturated) {
        std::cout << "  [RATE LIMIT]";
    }
    if (show_perception) {
        std::cout << "  src=" << t.perception_source;
        if (t.ai_presence_probability.has_value()) {
            std::cout << " ai_conf=" << std::fixed << std::setprecision(2) << *t.ai_presence_probability;
        }
        if (t.perception_rejection_reason != "NOT_APPLICABLE") {
            std::cout << " rejected=" << t.perception_rejection_reason;
        }
    }
    if (show_tracker) {
        std::cout << "  lock=" << t.tracker_lock_state;
        if (t.tracker_confidence.has_value()) {
            std::cout << " conf=" << std::fixed << std::setprecision(2) << *t.tracker_confidence;
        }
        if (t.tracker_is_prediction) {
            std::cout << " [PREDICTED]";
        }
    }
    std::cout << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);

    for (const std::string& arg : args) {
        if (arg == "--help" || arg == "-h") {
            std::cout << demo_help_text() << '\n';
            return 0;
        }
    }

    std::optional<DemoScenario> scenario;
    std::optional<double> duration_override;
    std::string csv_path;
    bool quiet = false;
    bool tracker_enabled = false;
    std::string mode_token = "classical";

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--tracker") {
            tracker_enabled = true;
        } else if (arg == "--mode") {
            if (i + 1 >= args.size()) {
                return usage_error("--mode needs a value (classical|ai|hybrid)");
            }
            mode_token = args[++i];
            if (!parse_perception_mode(mode_token).has_value()) {
                return usage_error("--mode must be one of: classical, ai, hybrid (got '" + mode_token + "')");
            }
        } else if (arg == "--duration") {
            if (i + 1 >= args.size()) {
                return usage_error("--duration needs a value");
            }
            const std::string& value = args[++i];
            try {
                std::size_t consumed = 0;
                const double seconds = std::stod(value, &consumed);
                if (consumed != value.size() || !(seconds > 0.0)) {
                    return usage_error("--duration must be a positive number of seconds");
                }
                duration_override = seconds;
            } catch (const std::exception&) {
                return usage_error("--duration must be a number");
            }
        } else if (arg == "--csv") {
            if (i + 1 >= args.size()) {
                return usage_error("--csv needs a path");
            }
            csv_path = args[++i];
        } else if (!arg.empty() && arg.front() == '-') {
            return usage_error("unknown option: " + arg);
        } else if (!scenario.has_value()) {
            scenario = parse_demo_scenario(arg);
            if (!scenario.has_value()) {
                return usage_error("unknown scenario: '" + arg + "'");
            }
        } else {
            return usage_error("unexpected extra argument: '" + arg + "'");
        }
    }

    if (!scenario.has_value()) {
        return usage_error("no scenario given");
    }

    const PerceptionMode requested_mode = *parse_perception_mode(mode_token);
    const double duration_s = duration_override.value_or(demo_scenario_duration_s(*scenario));

    // MODEL FAILURE handling (Phase 7): if AI/Hybrid mode is requested but the
    // ONNX model can't be loaded (missing file, bad build, wrong CWD), fall
    // back to the validated Classical baseline rather than crashing the demo.
    // The fallback is loud (stderr) and visible in the startup banner below —
    // never a silent degradation.
    PerceptionMode active_mode = requested_mode;
    std::optional<AiBeaconDetectorConfig> active_ai_detector;
    bool active_tracker_enabled = tracker_enabled;
    std::unique_ptr<DemoSession> session_ptr;
    if (requested_mode != PerceptionMode::Classical || tracker_enabled) {
        if (requested_mode != PerceptionMode::Classical) {
            active_ai_detector = default_ai_detector_config();
        }
        try {
            session_ptr = std::make_unique<DemoSession>(
                *scenario, duration_s, requested_mode, active_ai_detector, tracker_enabled);
        } catch (const std::exception& e) {
            std::cerr << "fsoc_demo: WARNING: could not start in --mode " << mode_token
                      << (tracker_enabled ? " --tracker" : "") << " (" << e.what() << ")\n"
                      << "fsoc_demo: falling back to --mode classical, tracker off"
                      << (active_ai_detector.has_value() ? " (model path: " + active_ai_detector->model_path + ")"
                                                          : "")
                      << "\n\n";
            active_mode = PerceptionMode::Classical;
            active_ai_detector.reset();
            active_tracker_enabled = false;
        }
    }
    if (!session_ptr) {
        session_ptr = std::make_unique<DemoSession>(*scenario, duration_s);
    }
    DemoSession& session = *session_ptr;

    std::optional<CsvTelemetryLogger> logger;
    if (!csv_path.empty()) {
        logger.emplace(csv_path);
        logger->write_header();
    }

    std::cout << "scenario : " << to_string(session.scenario()) << "  ("
              << demo_scenario_token(session.scenario()) << ")\n"
              << "detail   : " << demo_scenario_description(session.scenario()) << "\n"
              << "duration : " << std::fixed << std::setprecision(2) << session.duration_s()
              << " s  (" << session.total_frames() << " frames @ 50 Hz, dt = 0.02 s)\n"
              << "control  : "
              << (session.runner_config().control_enabled ? "ENABLED (closed loop)"
                                                          : "DISABLED (open loop)")
              << "\n"
              << "perception: " << to_string(active_mode)
              << (active_mode == PerceptionMode::Classical
                      ? "  (validated v1 baseline)"
                      : "  (Stage-3 C++ ONNX inference, models/tiny_beacon_net.onnx)")
              << "\n"
              << "tracker  : " << (active_tracker_enabled ? "ENABLED (alpha-beta estimator + temporal gate)"
                                                            : "disabled")
              << "\n\n";

    std::vector<TelemetryRecord> records;
    records.reserve(session.total_frames());

    constexpr std::size_t kPrintEvery = 25;  // 0.5 s at 50 Hz
    const auto wall_start = std::chrono::steady_clock::now();

    while (!session.finished()) {
        const DemoSnapshot snapshot = session.step();
        records.push_back(session.last_telemetry());
        if (logger.has_value()) {
            logger->record(session.last_telemetry());
        }
        if (!quiet && (snapshot.frame_index % kPrintEvery == 0 || session.finished())) {
            print_status_line(
                snapshot, session.last_telemetry(), active_mode != PerceptionMode::Classical,
                active_tracker_enabled);
        }
    }

    const auto wall_end = std::chrono::steady_clock::now();
    const double wall_s = std::chrono::duration<double>(wall_end - wall_start).count();

    const BenchmarkMetrics metrics = compute_benchmark_metrics(records, wall_s);

    std::cout << "\n--- summary : " << to_string(session.scenario()) << " ---\n"
              << "frames             : " << metrics.frames << "\n"
              << "detection          : " << std::fixed << std::setprecision(1)
              << 100.0 * metrics.detection_fraction << " %\n"
              << "RMS angular error  : " << std::setprecision(4)
              << rad_to_deg(metrics.rms_angular_error_rad) << " deg\n"
              << "P95 angular error  : " << rad_to_deg(metrics.p95_angular_error_rad) << " deg\n"
              << "max angular error  : " << rad_to_deg(metrics.max_angular_error_rad) << " deg\n"
              << "final angular error: " << rad_to_deg(metrics.final_angular_error_rad) << " deg\n"
              << "lost frames        : " << metrics.lost_frames << " / " << metrics.frames << "\n";
    if (logger.has_value()) {
        std::cout << "csv                : " << csv_path << "  (" << logger->records_written()
                  << " rows)\n";
    }
    std::cout << "clocks             : simulation 50 Hz (authoritative)  |  processing "
              << std::setprecision(0) << metrics.processing_fps << " FPS (wall, informational)\n";

    return 0;
}
