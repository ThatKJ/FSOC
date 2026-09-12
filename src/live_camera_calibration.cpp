#include "fsoc/live_camera_calibration.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fsoc {

void LiveCameraCalibrationConfig::validate() const {
    if (width_px <= 0 || height_px <= 0) {
        throw std::invalid_argument("LiveCameraCalibrationConfig: width_px/height_px must be > 0.");
    }
    if (!std::isfinite(hfov_deg) || !(hfov_deg > 0.0 && hfov_deg < 180.0)) {
        throw std::invalid_argument("LiveCameraCalibrationConfig: hfov_deg must be finite in (0, 180).");
    }
    if (!std::isfinite(vfov_deg) || !(vfov_deg > 0.0 && vfov_deg < 180.0)) {
        throw std::invalid_argument("LiveCameraCalibrationConfig: vfov_deg must be finite in (0, 180).");
    }
}

namespace {
std::unordered_map<std::string, std::string> parse_key_value_lines(std::istream& in) {
    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        auto trim = [](std::string s) {
            const auto first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return std::string{};
            const auto last = s.find_last_not_of(" \t\r\n");
            return s.substr(first, last - first + 1);
        };
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (!key.empty()) {
            kv[key] = value;
        }
    }
    return kv;
}
}  // namespace

LiveCameraCalibrationConfig load_live_camera_calibration(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("load_live_camera_calibration: cannot open '" + path + "'.");
    }
    const auto kv = parse_key_value_lines(file);

    auto require = [&](const char* key) -> const std::string& {
        const auto it = kv.find(key);
        if (it == kv.end()) {
            throw std::invalid_argument(
                std::string("load_live_camera_calibration: missing required key '") + key + "'.");
        }
        return it->second;
    };

    LiveCameraCalibrationConfig config{};
    try {
        config.width_px = std::stoi(require("width_px"));
        config.height_px = std::stoi(require("height_px"));
        config.hfov_deg = std::stod(require("hfov_deg"));
        config.vfov_deg = std::stod(require("vfov_deg"));
    } catch (const std::invalid_argument&) {
        throw;
    } catch (const std::exception& e) {
        throw std::invalid_argument(
            std::string("load_live_camera_calibration: malformed numeric value (") + e.what() + ").");
    }
    config.validate();
    return config;
}

void save_live_camera_calibration(const std::string& path, const LiveCameraCalibrationConfig& config) {
    config.validate();
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("save_live_camera_calibration: cannot write '" + path + "'.");
    }
    file << "# FSOC live-camera calibration (Phone Camera-in-the-Loop milestone)\n"
         << "# Declares the pinhole geometry used ONLY for real-camera pixel->angle\n"
         << "# conversion (fsoc::compute_tracking_error). Never assumed equal to the\n"
         << "# synthetic simulation's CameraConfig.\n"
         << "width_px=" << config.width_px << "\n"
         << "height_px=" << config.height_px << "\n"
         << "hfov_deg=" << config.hfov_deg << "\n"
         << "vfov_deg=" << config.vfov_deg << "\n";
}

CameraConfig to_camera_config(const LiveCameraCalibrationConfig& calibration) {
    calibration.validate();
    CameraConfig cfg{};
    cfg.width_px = calibration.width_px;
    cfg.height_px = calibration.height_px;
    cfg.hfov_rad = deg_to_rad(calibration.hfov_deg);
    cfg.vfov_rad = deg_to_rad(calibration.vfov_deg);
    // Actuator-rate/tilt-limit fields below are UNUSED placeholders — this
    // CameraConfig only ever backs a sensing-reference PanTiltCamera that is
    // never .step()-ed. Left at CameraConfig's own defaults deliberately,
    // rather than duplicating them here, so there is exactly one place
    // (fsoc/config.hpp) that defines what "default" means.
    cfg.validate();
    return cfg;
}

double estimate_hfov_deg_from_known_object(
    double object_width_m, double distance_m, double object_pixel_width_px, int image_width_px) {
    if (!std::isfinite(object_width_m) || object_width_m <= 0.0) {
        throw std::invalid_argument("estimate_hfov_deg_from_known_object: object_width_m must be finite > 0.");
    }
    if (!std::isfinite(distance_m) || distance_m <= 0.0) {
        throw std::invalid_argument("estimate_hfov_deg_from_known_object: distance_m must be finite > 0.");
    }
    if (!std::isfinite(object_pixel_width_px) || object_pixel_width_px <= 0.0) {
        throw std::invalid_argument(
            "estimate_hfov_deg_from_known_object: object_pixel_width_px must be finite > 0.");
    }
    if (image_width_px <= 0) {
        throw std::invalid_argument("estimate_hfov_deg_from_known_object: image_width_px must be > 0.");
    }
    const double theta_rad = 2.0 * std::atan((object_width_m / 2.0) / distance_m);
    const double hfov_rad = theta_rad * (static_cast<double>(image_width_px) / object_pixel_width_px);
    return rad_to_deg(hfov_rad);
}

double estimate_vfov_deg_from_hfov(double hfov_deg, int width_px, int height_px) {
    if (!std::isfinite(hfov_deg) || !(hfov_deg > 0.0 && hfov_deg < 180.0)) {
        throw std::invalid_argument("estimate_vfov_deg_from_hfov: hfov_deg must be finite in (0, 180).");
    }
    if (width_px <= 0 || height_px <= 0) {
        throw std::invalid_argument("estimate_vfov_deg_from_hfov: width_px/height_px must be > 0.");
    }
    const double hfov_rad = deg_to_rad(hfov_deg);
    const double vfov_rad =
        2.0 * std::atan(std::tan(hfov_rad / 2.0) * (static_cast<double>(height_px) / static_cast<double>(width_px)));
    return rad_to_deg(vfov_rad);
}

}  // namespace fsoc
