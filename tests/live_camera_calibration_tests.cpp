// Mobile Phone Camera-in-the-Loop milestone: LiveCameraCalibrationConfig
// load/save/convert/estimate unit checks. Same lightweight harness as
// tests/step1_tests.cpp .. tests/step6_tests.cpp. OpenCV-free.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "fsoc/live_camera_calibration.hpp"

namespace {

using fsoc::LiveCameraCalibrationConfig;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

void check_near(
    const double actual, const double expected, const double tolerance, const std::string_view expression,
    const int line) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << " actual=" << actual
                  << " expected=" << expected << " tol=" << tolerance << '\n';
    }
}

template <typename Fn>
void check_throws(Fn&& fn, const std::string_view expression, const int line) {
    try {
        fn();
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << " expected to throw, did not\n";
    } catch (const std::exception&) {
        // expected (either std::invalid_argument or std::runtime_error, depending on the check)
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_NEAR(actual, expected, tol) check_near((actual), (expected), (tol), #actual, __LINE__)
#define CHECK_THROWS(expr) check_throws([&] { (void)(expr); }, #expr, __LINE__)

void test_validate_rejects_bad_values() {
    LiveCameraCalibrationConfig bad_width{};
    bad_width.width_px = 0;
    CHECK_THROWS(bad_width.validate());

    LiveCameraCalibrationConfig bad_hfov{};
    bad_hfov.hfov_deg = 0.0;
    CHECK_THROWS(bad_hfov.validate());

    LiveCameraCalibrationConfig bad_hfov2{};
    bad_hfov2.hfov_deg = 200.0;
    CHECK_THROWS(bad_hfov2.validate());

    LiveCameraCalibrationConfig ok{};
    ok.validate();  // must not throw
}

std::string temp_path(const char* filename) {
    return (std::filesystem::temp_directory_path() / filename).string();
}

void test_save_load_roundtrip() {
    const std::string path = temp_path("fsoc_test_live_camera_calibration_roundtrip.cfg");
    LiveCameraCalibrationConfig original{};
    original.width_px = 1920;
    original.height_px = 1080;
    original.hfov_deg = 68.5;
    original.vfov_deg = 41.2;

    fsoc::save_live_camera_calibration(path, original);
    const LiveCameraCalibrationConfig loaded = fsoc::load_live_camera_calibration(path);

    CHECK(loaded.width_px == original.width_px);
    CHECK(loaded.height_px == original.height_px);
    CHECK_NEAR(loaded.hfov_deg, original.hfov_deg, 1e-9);
    CHECK_NEAR(loaded.vfov_deg, original.vfov_deg, 1e-9);

    std::remove(path.c_str());
}

void test_load_missing_file_throws() {
    CHECK_THROWS(fsoc::load_live_camera_calibration(temp_path("fsoc_test_does_not_exist_calibration.cfg")));
}

void test_load_missing_key_throws() {
    const std::string path = temp_path("fsoc_test_live_camera_calibration_missing_key.cfg");
    {
        std::FILE* f = std::fopen(path.c_str(), "w");
        std::fputs("width_px=640\nheight_px=480\n# hfov_deg missing\nvfov_deg=30\n", f);
        std::fclose(f);
    }
    CHECK_THROWS(fsoc::load_live_camera_calibration(path));
    std::remove(path.c_str());
}

void test_to_camera_config() {
    LiveCameraCalibrationConfig calibration{};
    calibration.width_px = 1280;
    calibration.height_px = 720;
    calibration.hfov_deg = 60.0;
    calibration.vfov_deg = 34.0;

    const fsoc::CameraConfig camera = fsoc::to_camera_config(calibration);
    CHECK(camera.width_px == 1280);
    CHECK(camera.height_px == 720);
    CHECK_NEAR(camera.hfov_rad, fsoc::deg_to_rad(60.0), 1e-9);
    CHECK_NEAR(camera.vfov_rad, fsoc::deg_to_rad(34.0), 1e-9);
}

void test_estimate_hfov_from_known_object() {
    // A 1m-wide object at 1m distance subtends 2*atan(0.5/1) = 53.13 deg.
    // If that fills exactly half the image width, hfov = 2x that = 106.26 deg.
    const double hfov = fsoc::estimate_hfov_deg_from_known_object(
        /*object_width_m=*/1.0, /*distance_m=*/1.0, /*object_pixel_width_px=*/960.0,
        /*image_width_px=*/1920);
    CHECK_NEAR(hfov, 106.26, 0.05);

    CHECK_THROWS(fsoc::estimate_hfov_deg_from_known_object(0.0, 1.0, 100.0, 1920));
    CHECK_THROWS(fsoc::estimate_hfov_deg_from_known_object(1.0, 0.0, 100.0, 1920));
    CHECK_THROWS(fsoc::estimate_hfov_deg_from_known_object(1.0, 1.0, 0.0, 1920));
    CHECK_THROWS(fsoc::estimate_hfov_deg_from_known_object(1.0, 1.0, 100.0, 0));
}

void test_estimate_vfov_from_hfov_square_image_matches_hfov() {
    // A square image (width == height) with square pixels must have vfov == hfov.
    const double vfov = fsoc::estimate_vfov_deg_from_hfov(60.0, 1000, 1000);
    CHECK_NEAR(vfov, 60.0, 1e-9);

    CHECK_THROWS(fsoc::estimate_vfov_deg_from_hfov(0.0, 1000, 1000));
    CHECK_THROWS(fsoc::estimate_vfov_deg_from_hfov(60.0, 0, 1000));
}

}  // namespace

int main() {
    test_validate_rejects_bad_values();
    test_save_load_roundtrip();
    test_load_missing_file_throws();
    test_load_missing_key_throws();
    test_to_camera_config();
    test_estimate_hfov_from_known_object();
    test_estimate_vfov_from_hfov_square_image_matches_hfov();

    if (failures == 0) {
        std::cout << "PASS: 7 LiveCameraCalibration checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
