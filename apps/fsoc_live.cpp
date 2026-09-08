// fsoc_live — Mobile Phone Camera-in-the-Loop.
//
//   REAL TARGET  ->  MOBILE PHONE CAMERA  ->  REAL VIDEO FRAME
//     -> Hybrid Perception -> State Estimator -> Prediction -> Controller
//     -> VirtualPanTiltActuator -> REAL CONTROL COMMAND TELEMETRY
//
// This is a real-camera-in-the-loop PROTOTYPE, not a physical closed loop:
// the sensing side (camera + perception + estimation) is fully real; the
// actuator side is HONESTLY VIRTUAL (see fsoc/virtual_actuator.hpp) — no
// servo, gimbal, or physical pan/tilt hardware exists or is claimed. See
// docs/PHONE_CAMERA_METRICS.md and README.md's claim-boundary section.
//
// Usage:
//   fsoc_live --source camera --camera-index 0 --calibration configs/phone_camera.cfg
//   fsoc_live --source camera-url --camera-url "<url>" --calibration configs/phone_camera.cfg
//
// Options:
//   --mode classical|ai|hybrid   (default classical)
//   --tracker                    enable the P0-v2 alpha-beta estimator
//   --manual-assist              print human-readable PAN/TILT correction cues
//   --no-control                 observe-only: never compute/apply a command
//   --seconds N                  stop after N seconds (default: run until Ctrl+C / camera ends)
//   --live-out DIR               where telemetry.json / frame.jpg are written (default generated/live)
//   --ai-model PATH              required when --mode ai or --mode hybrid
//
// Run this YOURSELF, interactively — opening a camera device may trigger an
// OS permission prompt only a real interactive session can answer.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "fsoc/config.hpp"
#include "fsoc/live_camera_calibration.hpp"
#include "fsoc/live_tracking_session.hpp"
#include "fsoc/opencv_camera_frame_source.hpp"

namespace {

struct Args {
    std::optional<int> camera_index{};
    std::optional<std::string> camera_url{};
    std::string calibration_path{};
    fsoc::PerceptionMode mode{fsoc::PerceptionMode::Classical};
    std::string ai_model_path{};
    bool tracker = false;
    bool manual_assist = false;
    bool control_enabled = true;
    double seconds = -1.0;  // -1 = run until stopped
    std::string live_out = "generated/live";
};

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  fsoc_live --source camera --camera-index N --calibration PATH [options]\n"
        << "  fsoc_live --source camera-url --camera-url URL --calibration PATH [options]\n"
        << "Options:\n"
        << "  --mode classical|ai|hybrid   (default classical)\n"
        << "  --tracker                    enable the P0-v2 alpha-beta estimator\n"
        << "  --manual-assist              print human-readable PAN/TILT correction cues\n"
        << "  --no-control                 observe-only: never compute/apply a command\n"
        << "  --seconds N                  stop after N seconds (default: run until stopped)\n"
        << "  --live-out DIR               telemetry.json / frame.jpg output dir\n"
        << "  --ai-model PATH              required for --mode ai / --mode hybrid\n";
}

std::optional<Args> parse_args(int argc, char** argv) {
    Args args{};
    std::optional<std::string> source{};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string{}; };
        if (arg == "--source") source = next();
        else if (arg == "--camera-index") args.camera_index = std::stoi(next());
        else if (arg == "--camera-url") args.camera_url = next();
        else if (arg == "--calibration") args.calibration_path = next();
        else if (arg == "--mode") {
            const auto v = next();
            if (v == "classical") args.mode = fsoc::PerceptionMode::Classical;
            else if (v == "ai") args.mode = fsoc::PerceptionMode::AI;
            else if (v == "hybrid") args.mode = fsoc::PerceptionMode::Hybrid;
            else { std::cerr << "--mode must be classical|ai|hybrid\n"; return std::nullopt; }
        }
        else if (arg == "--ai-model") args.ai_model_path = next();
        else if (arg == "--tracker") args.tracker = true;
        else if (arg == "--manual-assist") args.manual_assist = true;
        else if (arg == "--no-control") args.control_enabled = false;
        else if (arg == "--seconds") args.seconds = std::stod(next());
        else if (arg == "--live-out") args.live_out = next();
        else if (arg == "--help" || arg == "-h") { print_usage(); std::exit(0); }
        else { std::cerr << "unrecognized argument '" << arg << "'\n"; print_usage(); return std::nullopt; }
    }

    if (!source.has_value() || (*source != "camera" && *source != "camera-url")) {
        std::cerr << "--source must be 'camera' or 'camera-url'\n";
        return std::nullopt;
    }
    if (*source == "camera" && !args.camera_index.has_value()) {
        std::cerr << "--source camera requires --camera-index N\n";
        return std::nullopt;
    }
    if (*source == "camera-url" && !args.camera_url.has_value()) {
        std::cerr << "--source camera-url requires --camera-url URL\n";
        return std::nullopt;
    }
    if (args.calibration_path.empty()) {
        std::cerr << "--calibration PATH is required (see fsoc_camera_calibrate)\n";
        return std::nullopt;
    }
    if (args.mode != fsoc::PerceptionMode::Classical && args.ai_model_path.empty()) {
        std::cerr << "--mode " << (args.mode == fsoc::PerceptionMode::AI ? "ai" : "hybrid")
                  << " requires --ai-model PATH\n";
        return std::nullopt;
    }
    return args;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string opt_json(const std::optional<double>& v) {
    if (!v.has_value()) return "null";
    std::ostringstream os;
    os << std::setprecision(9) << *v;
    return os.str();
}

// Hand-rolled, minimal JSON (no JSON library is linked into the C++ core —
// see docs/PHONE_CAMERA_METRICS.md for why). Schema documented in
// docs/PHONE_CAMERA_METRICS.md "Live telemetry JSON schema".
std::string to_json(const fsoc::LiveFrameResult& r, fsoc::PerceptionMode mode, bool control_enabled) {
    std::ostringstream j;
    j << std::fixed << std::setprecision(6);
    j << "{\n"
      << "  \"frameIndex\": " << r.frame_index << ",\n"
      << "  \"timestampS\": " << r.timestamp_s << ",\n"
      << "  \"dtS\": " << r.dt_s << ",\n"
      << "  \"cameraSource\": \"REAL_PHONE_CAMERA\",\n"
      << "  \"actuatorType\": \"VIRTUAL\",\n"
      << "  \"sourceKind\": \"" << fsoc::to_string(r.source.kind) << "\",\n"
      << "  \"sourceBackend\": \"" << json_escape(r.source.backend_name) << "\",\n"
      << "  \"sourceDescription\": \"" << json_escape(r.source.description) << "\",\n"
      << "  \"rawWidthPx\": " << r.raw_width_px << ",\n"
      << "  \"rawHeightPx\": " << r.raw_height_px << ",\n"
      << "  \"preprocessedWidthPx\": " << r.preprocessed_width_px << ",\n"
      << "  \"preprocessedHeightPx\": " << r.preprocessed_height_px << ",\n"
      << "  \"perceptionMode\": \"" << fsoc::to_string(mode) << "\",\n"
      << "  \"perceptionSource\": \"" << fsoc::to_string(r.perception.perception_source) << "\",\n"
      << "  \"classicalDetected\": " << (r.perception.classical_detected ? "true" : "false") << ",\n"
      << "  \"aiCandidateDetected\": " << (r.perception.ai_candidate_detected ? "true" : "false") << ",\n"
      << "  \"aiPresenceProbability\": " << opt_json(r.perception.ai_presence_probability) << ",\n"
      << "  \"targetDetected\": " << (r.target_detected ? "true" : "false") << ",\n"
      << "  \"detectedXPx\": "
      << (r.detection.has_value() ? std::to_string(r.detection->centroid_px.x_px) : "null") << ",\n"
      << "  \"detectedYPx\": "
      << (r.detection.has_value() ? std::to_string(r.detection->centroid_px.y_px) : "null") << ",\n"
      << "  \"pixelErrorXPx\": " << (r.tracking_error.has_value() ? std::to_string(r.tracking_error->pixel.x_px) : "null") << ",\n"
      << "  \"pixelErrorYPx\": " << (r.tracking_error.has_value() ? std::to_string(r.tracking_error->pixel.y_px) : "null") << ",\n"
      << "  \"panErrorDeg\": " << (r.tracking_error.has_value() ? std::to_string(fsoc::rad_to_deg(r.tracking_error->angular.pan_rad)) : "null") << ",\n"
      << "  \"tiltErrorDeg\": " << (r.tracking_error.has_value() ? std::to_string(fsoc::rad_to_deg(r.tracking_error->angular.tilt_rad)) : "null") << ",\n"
      << "  \"totalErrorDeg\": "
      << (r.tracking_error.has_value()
              ? std::to_string(fsoc::rad_to_deg(
                    std::hypot(r.tracking_error->angular.pan_rad, r.tracking_error->angular.tilt_rad)))
              : "null")
      << ",\n"
      << "  \"lockState\": \"" << fsoc::to_string(r.tracked_state.lock_state) << "\",\n"
      << "  \"trackerConfidence\": " << r.tracked_state.confidence << ",\n"
      << "  \"isPrediction\": " << (r.tracked_state.is_prediction ? "true" : "false") << ",\n"
      << "  \"controlEnabled\": " << (control_enabled ? "true" : "false") << ",\n"
      << "  \"commandPanRateDegS\": " << fsoc::rad_to_deg(r.command.pan_rate_rad_s) << ",\n"
      << "  \"commandTiltRateDegS\": " << fsoc::rad_to_deg(r.command.tilt_rate_rad_s) << ",\n"
      << "  \"virtualPanDeg\": " << fsoc::rad_to_deg(r.actuator_state.pan_rad) << ",\n"
      << "  \"virtualTiltDeg\": " << fsoc::rad_to_deg(r.actuator_state.tilt_rad) << ",\n"
      << "  \"virtualPanSaturated\": " << (r.actuator_state.pan_saturated ? "true" : "false") << ",\n"
      << "  \"virtualTiltSaturated\": " << (r.actuator_state.tilt_saturated ? "true" : "false") << "\n"
      << "}\n";
    return j.str();
}

void print_manual_assist_cue(const fsoc::LiveFrameResult& r) {
    if (!r.tracking_error.has_value()) {
        std::cout << "  [manual-assist] no target -- hold steady / re-acquire\n";
        return;
    }
    const double pan_deg = fsoc::rad_to_deg(r.tracking_error->angular.pan_rad);
    const double tilt_deg = fsoc::rad_to_deg(r.tracking_error->angular.tilt_rad);
    // Sign convention (frozen, fsoc/tracking_error.hpp): pan_rad > 0 => beacon
    // RIGHT => centering requires panning the camera toward +pan (right).
    // tilt_rad > 0 => beacon ABOVE => centering requires tilting toward
    // +tilt (up). The cue tells a human which way to turn the phone to
    // reduce this error, not which way the beacon moved.
    const char* pan_dir = pan_deg >= 0.0 ? "PAN RIGHT ->" : "<- PAN LEFT ";
    const char* tilt_dir = tilt_deg >= 0.0 ? "TILT UP" : "TILT DOWN";
    std::cout << std::fixed << std::setprecision(1) << "  REQUIRED CORRECTION   " << pan_dir << " "
              << std::abs(pan_deg) << " deg      " << tilt_dir << " " << std::abs(tilt_deg) << " deg\n";
}

}  // namespace

int main(int argc, char** argv) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed.has_value()) {
        return 2;
    }
    const Args& args = *parsed;

    fsoc::LiveCameraCalibrationConfig calibration{};
    try {
        calibration = fsoc::load_live_camera_calibration(args.calibration_path);
    } catch (const std::exception& e) {
        std::cerr << "fsoc_live: failed to load calibration: " << e.what() << "\n"
                  << "  Run fsoc_camera_calibrate first (see docs/PHONE_CAMERA_METRICS.md).\n";
        return 1;
    }

    fsoc::LiveTrackingSessionConfig session_config{};
    session_config.calibration = calibration;
    session_config.perception_mode = args.mode;
    session_config.tracker_enabled = args.tracker;
    session_config.control_enabled = args.control_enabled;
    if (args.mode != fsoc::PerceptionMode::Classical) {
        fsoc::AiBeaconDetectorConfig ai_config{};
        ai_config.model_path = args.ai_model_path;
        session_config.ai_detector = ai_config;
    }

    std::unique_ptr<fsoc::LiveTrackingSession> session;
    try {
        session = std::make_unique<fsoc::LiveTrackingSession>(session_config);
    } catch (const std::exception& e) {
        std::cerr << "fsoc_live: failed to construct tracking session: " << e.what() << "\n";
        if (args.mode != fsoc::PerceptionMode::Classical) {
            std::cerr << "  Check --ai-model points at a valid ONNX file (e.g. models/tiny_beacon_net.onnx).\n";
        }
        return 1;
    }

    fsoc::OpenCVCameraFrameSourceConfig source_config{};
    source_config.camera_index = args.camera_index;
    source_config.url = args.camera_url;
    fsoc::OpenCVCameraFrameSource source(source_config);

    if (!source.open()) {
        std::cerr << "fsoc_live: FAILED to open camera source.\n"
                  << "  Run fsoc_camera_probe first to find an AVAILABLE index, and confirm OS\n"
                  << "  camera permission is granted.\n";
        return 1;
    }
    const fsoc::FrameSourceInfo source_info = source.info();

    std::system(("mkdir -p " + args.live_out).c_str());
    std::cout << "FSOC LIVE -- CAMERA SOURCE = REAL_PHONE_CAMERA   ACTUATOR = VIRTUAL\n";
    std::cout << "Source: " << source_info.backend_name << " " << source_info.width_px << "x"
              << source_info.height_px << "  mode=" << fsoc::to_string(args.mode)
              << "  tracker=" << (args.tracker ? "on" : "off")
              << "  control=" << (args.control_enabled ? "on" : "off") << "\n";
    std::cout << "Telemetry: " << args.live_out << "/telemetry.json + " << args.live_out
              << "/frame.jpg (polled by Mission Control)\n";
    std::cout << "Press Ctrl+C to stop.\n\n";

    const auto start = std::chrono::steady_clock::now();
    auto last_frame_time = start;
    std::size_t consecutive_failures = 0;
    constexpr std::size_t kMaxConsecutiveFailures = 60;

    fsoc::Frame raw_frame{};
    while (true) {
        const double elapsed_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (args.seconds > 0.0 && elapsed_s >= args.seconds) {
            std::cout << "Reached --seconds limit. Stopping.\n";
            break;
        }

        if (!source.read(raw_frame)) {
            ++consecutive_failures;
            if (consecutive_failures >= kMaxConsecutiveFailures) {
                std::cerr << "fsoc_live: too many consecutive frame read failures -- camera appears "
                             "disconnected. Stopping cleanly (no crash, no fabricated frames).\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        consecutive_failures = 0;

        const auto now = std::chrono::steady_clock::now();
        const double dt_s = std::chrono::duration<double>(now - last_frame_time).count();
        last_frame_time = now;
        if (dt_s <= 0.0) {
            continue;  // clock hasn't advanced yet on the very first iteration path; skip, don't crash
        }

        fsoc::LiveFrameResult result;
        try {
            result = session->process_frame(raw_frame, source_info, dt_s);
        } catch (const std::exception& e) {
            std::cerr << "fsoc_live: frame " << raw_frame.frame_index
                      << " failed to process (" << e.what() << ") -- skipping this frame.\n";
            continue;
        }

        std::ofstream telemetry_file(args.live_out + "/telemetry.json", std::ios::trunc);
        telemetry_file << to_json(result, args.mode, args.control_enabled);
        telemetry_file.close();
        cv::imwrite(args.live_out + "/frame.jpg", raw_frame.image);

        std::cout << std::fixed << std::setprecision(2) << "frame " << result.frame_index << "  t="
                  << result.timestamp_s << "s  lock=" << fsoc::to_string(result.tracked_state.lock_state)
                  << "  detected=" << (result.target_detected ? "yes" : "no");
        if (result.tracking_error.has_value()) {
            std::cout << "  error="
                      << std::hypot(fsoc::rad_to_deg(result.tracking_error->angular.pan_rad),
                                    fsoc::rad_to_deg(result.tracking_error->angular.tilt_rad))
                      << "deg";
        }
        std::cout << "\n";
        if (args.manual_assist) {
            print_manual_assist_cue(result);
        }
    }

    source.close();
    return 0;
}
