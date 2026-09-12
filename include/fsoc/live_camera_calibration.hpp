#pragma once

#include <string>

#include "fsoc/config.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// LiveCameraCalibration — the simplest defensible camera geometry (Phase 8)
// ---------------------------------------------------------------------------
//
// Deliberately NOT a photogrammetry suite: a pinhole model declared by four
// numbers (resolution + horizontal/vertical field of view), stored in a
// dependency-free `key=value` text file (the C++ core links no JSON library;
// see docs/PHONE_CAMERA_METRICS.md for why this format was chosen over adding
// one just for this milestone).
//
// This is NEVER assumed to equal the simulation's CameraConfig (deg_to_rad(20)
// / deg_to_rad(15)) — a phone camera's real FOV must be declared or measured,
// never silently inherited from the synthetic baseline.

struct LiveCameraCalibrationConfig {
    int width_px{1920};
    int height_px{1080};
    double hfov_deg{60.0};  // PLACEHOLDER — must be set from the phone's real spec or measured
    double vfov_deg{40.0};  // PLACEHOLDER — see estimate_vfov_deg_from_hfov below

    // width_px/height_px > 0; hfov_deg/vfov_deg finite in (0, 180). Throws
    // std::invalid_argument otherwise.
    void validate() const;
};

// Reads a `key=value` calibration file (one entry per line, `#` starts a
// comment, blank lines ignored). Recognized keys: width_px, height_px,
// hfov_deg, vfov_deg. Throws std::runtime_error if the file cannot be opened,
// std::invalid_argument if a required key is missing or a value fails
// validate().
[[nodiscard]] LiveCameraCalibrationConfig load_live_camera_calibration(const std::string& path);

// Writes the same format load_live_camera_calibration() reads, with a header
// comment. Throws std::runtime_error if the file cannot be written.
void save_live_camera_calibration(const std::string& path, const LiveCameraCalibrationConfig& config);

// Converts a declared/measured field of view into a CameraConfig at the
// calibration's OWN width_px/height_px (its declared/raw resolution) — a
// direct, dimensionally self-consistent conversion. The actuator-rate/tilt-
// limit fields on the returned CameraConfig are UNUSED placeholders.
//
// IMPORTANT: LiveTrackingSession does NOT use this function to build its
// sensing-reference camera. PanTiltCamera::fx_px()/fy_px() scale with
// width_px/height_px for a fixed hfov/vfov (fx_px = (width_px/2) /
// tan(hfov/2)), so cx/cy/fx/fy must be computed at whatever resolution the
// DETECTOR actually measures pixels in — the preprocessed frame size
// (LivePreprocessConfig), which is usually smaller than the phone's raw
// capture resolution. Reusing the raw resolution here would silently scale
// every angular error by the raw/preprocessed size ratio. See
// live_tracking_session.cpp's internal sensing_camera_config() for the
// conversion LiveTrackingSession actually performs. This function remains
// useful for calibration tooling (fsoc_camera_calibrate --check) and any
// future consumer that runs a detector directly on undownsampled frames.
[[nodiscard]] CameraConfig to_camera_config(const LiveCameraCalibrationConfig& calibration);

// Simple angular-substitution calibration: given a known real-world object of
// `object_width_m` at `distance_m`, spanning `object_pixel_width_px` pixels in
// a frame `image_width_px` wide, estimate the camera's horizontal field of
// view. Method: the object subtends angle
// theta = 2*atan((object_width_m/2) / distance_m); assuming that angle scales
// linearly with pixels near the image centre, hfov = theta * (image_width_px /
// object_pixel_width_px). This is a first-order approximation (ignores lens
// distortion) — adequate for coarse alignment, not a substitute for a real
// checkerboard calibration. Throws std::invalid_argument for non-finite or
// non-positive inputs.
[[nodiscard]] double estimate_hfov_deg_from_known_object(
    double object_width_m,
    double distance_m,
    double object_pixel_width_px,
    int image_width_px);

// Derives vfov from an already-known hfov assuming square pixels (no separate
// vertical measurement): vfov = 2*atan(tan(hfov/2) * height_px/width_px).
// Throws std::invalid_argument for non-finite/non-positive inputs or
// hfov_deg outside (0, 180).
[[nodiscard]] double estimate_vfov_deg_from_hfov(double hfov_deg, int width_px, int height_px);

}  // namespace fsoc
