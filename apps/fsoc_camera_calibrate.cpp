// fsoc_camera_calibrate — the simplest defensible camera geometry (Phase 8).
// NOT a photogrammetry suite: either declare a manually-known FOV (from the
// phone's spec sheet) or estimate it from one known-size object at a known
// distance (angular-substitution method). Writes a dependency-free
// key=value calibration file that fsoc_live / fsoc_camera_calibrate --check
// can read back.
//
// Usage:
//   fsoc_camera_calibrate --manual --width 1920 --height 1080 \
//       --hfov-deg 69 --vfov-deg 42 --out configs/phone_camera.cfg
//
//   fsoc_camera_calibrate --from-object --object-width-m 0.05 --distance-m 1.0 \
//       --object-pixel-width-px 120 --image-width-px 1920 --image-height-px 1080 \
//       --out configs/phone_camera.cfg
//
//   fsoc_camera_calibrate --check configs/phone_camera.cfg

#include <iostream>
#include <optional>
#include <string>

#include "fsoc/live_camera_calibration.hpp"

namespace {

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  fsoc_camera_calibrate --manual --width W --height H --hfov-deg D --vfov-deg D2 --out PATH\n"
        << "  fsoc_camera_calibrate --from-object --object-width-m M --distance-m D \\\n"
        << "      --object-pixel-width-px P --image-width-px W --image-height-px H --out PATH\n"
        << "  fsoc_camera_calibrate --check PATH\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<std::string> mode{};
    int width = 0, height = 0;
    double hfov_deg = 0.0, vfov_deg = 0.0;
    double object_width_m = 0.0, distance_m = 0.0, object_pixel_width_px = 0.0;
    std::string out_path;
    std::string check_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string{}; };
        if (arg == "--manual") mode = "manual";
        else if (arg == "--from-object") mode = "from-object";
        else if (arg == "--check") { mode = "check"; check_path = next(); }
        else if (arg == "--width" || arg == "--image-width-px") width = std::stoi(next());
        else if (arg == "--height" || arg == "--image-height-px") height = std::stoi(next());
        else if (arg == "--hfov-deg") hfov_deg = std::stod(next());
        else if (arg == "--vfov-deg") vfov_deg = std::stod(next());
        else if (arg == "--object-width-m") object_width_m = std::stod(next());
        else if (arg == "--distance-m") distance_m = std::stod(next());
        else if (arg == "--object-pixel-width-px") object_pixel_width_px = std::stod(next());
        else if (arg == "--out") out_path = next();
        else if (arg == "--help" || arg == "-h") { print_usage(); return 0; }
        else { std::cerr << "unrecognized argument '" << arg << "'\n"; print_usage(); return 2; }
    }

    if (!mode.has_value()) {
        print_usage();
        return 2;
    }

    try {
        if (*mode == "check") {
            const fsoc::LiveCameraCalibrationConfig config = fsoc::load_live_camera_calibration(check_path);
            std::cout << "OK: " << check_path << "\n"
                      << "  " << config.width_px << "x" << config.height_px << "  hfov=" << config.hfov_deg
                      << "deg  vfov=" << config.vfov_deg << "deg\n";
            return 0;
        }

        fsoc::LiveCameraCalibrationConfig config{};
        if (*mode == "manual") {
            config.width_px = width;
            config.height_px = height;
            config.hfov_deg = hfov_deg;
            config.vfov_deg = vfov_deg;
        } else {
            config.width_px = width;
            config.height_px = height;
            config.hfov_deg = fsoc::estimate_hfov_deg_from_known_object(
                object_width_m, distance_m, object_pixel_width_px, width);
            config.vfov_deg = fsoc::estimate_vfov_deg_from_hfov(config.hfov_deg, width, height);
            std::cout << "Estimated (angular-substitution, no lens-distortion correction):\n"
                      << "  hfov_deg=" << config.hfov_deg << "  vfov_deg=" << config.vfov_deg << "\n"
                      << "  This is a first-order approximation -- adequate for coarse alignment,\n"
                      << "  not a substitute for a real checkerboard calibration.\n";
        }

        if (out_path.empty()) {
            std::cerr << "--out PATH is required\n";
            return 2;
        }
        fsoc::save_live_camera_calibration(out_path, config);
        std::cout << "Wrote " << out_path << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fsoc_camera_calibrate: " << e.what() << "\n";
        return 1;
    }
}
