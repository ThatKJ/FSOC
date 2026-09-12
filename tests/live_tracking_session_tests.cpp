// Mobile Phone Camera-in-the-Loop milestone: LiveTrackingSession unit checks.
// Same lightweight harness as tests/step1_tests.cpp .. tests/step7_tests.cpp.
//
// Uses ONLY fabricated cv::Mat frames (a bright circle painted at a known
// pixel position) -- no real camera, no OpenCV VideoCapture, no hardware
// access. This proves the WIRING (preprocess -> detect -> resolve_perception
// -> tracker -> tracking-error -> control -> virtual actuator) is correct;
// the individual modules' own semantics are already covered by
// tests/step5_tests.cpp, tests/target_tracker_tests.cpp,
// tests/hybrid_perception_tests.cpp, tests/step6_tests.cpp.

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "fsoc/live_tracking_session.hpp"

namespace {

using fsoc::LiveFrameResult;
using fsoc::LiveTrackingSession;
using fsoc::LiveTrackingSessionConfig;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

template <typename Fn>
void check_throws(Fn&& fn, const std::string_view expression, const int line) {
    try {
        fn();
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << " expected to throw, did not\n";
    } catch (const std::invalid_argument&) {
        // expected
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_THROWS(expr) check_throws([&] { (void)(expr); }, #expr, __LINE__)

// 640x480 CV_8UC3 frame (a real camera would typically be BGR) with a bright
// filled circle at (x_px, y_px) -- exercises the grayscale-conversion path in
// preprocess_live_frame() too, not just a pre-grayscale image.
cv::Mat make_frame_with_dot(double x_px, double y_px) {
    cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(5, 5, 5));
    cv::circle(frame, cv::Point(static_cast<int>(x_px), static_cast<int>(y_px)), 10,
               cv::Scalar(220, 220, 220), cv::FILLED);
    return frame;
}

cv::Mat make_blank_frame() {
    return cv::Mat(480, 640, CV_8UC3, cv::Scalar(5, 5, 5));
}

fsoc::Frame wrap(const cv::Mat& image, std::size_t frame_index) {
    return fsoc::Frame{.image = image, .frame_index = frame_index, .timestamp_s = 0.0};
}

fsoc::FrameSourceInfo test_source_info() {
    return fsoc::FrameSourceInfo{
        .kind = fsoc::FrameSourceKind::OpenCVCamera,
        .width_px = 640,
        .height_px = 480,
        .fps = 30.0,
        .backend_name = "TEST",
        .description = "fabricated test frame",
    };
}

LiveTrackingSessionConfig make_config() {
    LiveTrackingSessionConfig config{};
    config.calibration.width_px = 640;
    config.calibration.height_px = 480;
    config.calibration.hfov_deg = 20.0;
    config.calibration.vfov_deg = 15.0;
    return config;
}

void test_quadrant_signs() {
    LiveTrackingSession session(make_config());

    // Beacon RIGHT of centre -> pan_rad > 0 (frozen convention, fsoc/tracking_error.hpp).
    {
        const auto raw = wrap(make_frame_with_dot(420.0, 240.0), 0);
        const LiveFrameResult r = session.process_frame(raw, test_source_info(), 0.02);
        CHECK(r.target_detected);
        CHECK(r.tracking_error.has_value());
        CHECK(r.tracking_error->angular.pan_rad > 0.0);
    }
    // Beacon LEFT of centre -> pan_rad < 0.
    {
        const auto raw = wrap(make_frame_with_dot(220.0, 240.0), 1);
        const LiveFrameResult r = session.process_frame(raw, test_source_info(), 0.02);
        CHECK(r.tracking_error->angular.pan_rad < 0.0);
    }
    // Beacon ABOVE centre (smaller y_px) -> tilt_rad > 0.
    {
        const auto raw = wrap(make_frame_with_dot(320.0, 140.0), 2);
        const LiveFrameResult r = session.process_frame(raw, test_source_info(), 0.02);
        CHECK(r.tracking_error->angular.tilt_rad > 0.0);
    }
    // Beacon BELOW centre (larger y_px) -> tilt_rad < 0.
    {
        const auto raw = wrap(make_frame_with_dot(320.0, 340.0), 3);
        const LiveFrameResult r = session.process_frame(raw, test_source_info(), 0.02);
        CHECK(r.tracking_error->angular.tilt_rad < 0.0);
    }
}

void test_no_target_no_ground_truth_leak() {
    LiveTrackingSession session(make_config());
    const auto raw = wrap(make_blank_frame(), 0);
    const LiveFrameResult r = session.process_frame(raw, test_source_info(), 0.02);
    CHECK(!r.target_detected);
    CHECK(!r.detection.has_value());
    CHECK(!r.tracking_error.has_value());
    CHECK(r.command.pan_rate_rad_s == 0.0);
    CHECK(r.command.tilt_rate_rad_s == 0.0);
}

void test_control_disabled_never_commands() {
    LiveTrackingSessionConfig config = make_config();
    config.control_enabled = false;
    LiveTrackingSession session(config);

    const auto raw = wrap(make_frame_with_dot(450.0, 240.0), 0);  // large, unambiguous offset
    const LiveFrameResult r = session.process_frame(raw, test_source_info(), 0.02);
    CHECK(r.target_detected);
    CHECK(r.tracking_error.has_value());
    CHECK(r.command.pan_rate_rad_s == 0.0);
    CHECK(r.command.tilt_rate_rad_s == 0.0);
    // The virtual actuator must not move either, since it only ever receives
    // the (always-zero, here) command.
    CHECK(r.actuator_state.pan_rad == 0.0);
}

void test_control_enabled_commands_toward_target() {
    LiveTrackingSession session(make_config());
    // Beacon well right of centre -> expect a RIGHT (positive) pan command,
    // matching the frozen sign convention the simulation itself relies on.
    const auto raw = wrap(make_frame_with_dot(450.0, 240.0), 0);
    const LiveFrameResult r = session.process_frame(raw, test_source_info(), 0.02);
    CHECK(r.command.pan_rate_rad_s > 0.0);
    CHECK(r.actuator_state.pan_rate_rad_s > 0.0);
    CHECK(r.actuator_state.pan_rad > 0.0);  // integrated at least one step in the commanded direction
}

void test_tracker_wiring_coasts_through_brief_gap() {
    LiveTrackingSessionConfig config = make_config();
    config.tracker_enabled = true;
    LiveTrackingSession session(config);

    // Acquire: acquire_frames_required consecutive consistent measurements
    // (default 3) at a stationary position.
    fsoc::LiveFrameResult r{};
    for (std::size_t i = 0; i < 3; ++i) {
        r = session.process_frame(wrap(make_frame_with_dot(400.0, 240.0), i), test_source_info(), 0.02);
    }
    CHECK(r.tracked_state.lock_state == fsoc::LockState::Tracking);

    // One blank frame within max_coast_frames (default 2) -> COASTING, still
    // "detected" (a predicted position), never leaking a fabricated real
    // measurement.
    r = session.process_frame(wrap(make_blank_frame(), 3), test_source_info(), 0.02);
    CHECK(r.tracked_state.lock_state == fsoc::LockState::Coasting);
    CHECK(r.tracked_state.is_prediction);

    // Real measurement returns -> back to Tracking.
    r = session.process_frame(wrap(make_frame_with_dot(400.0, 240.0), 4), test_source_info(), 0.02);
    CHECK(r.tracked_state.lock_state == fsoc::LockState::Tracking);
    CHECK(!r.tracked_state.is_prediction);
}

void test_reset_clears_controller_and_actuator_state() {
    LiveTrackingSession session(make_config());
    (void)session.process_frame(wrap(make_frame_with_dot(450.0, 240.0), 0), test_source_info(), 0.02);
    session.reset();
    // After reset, a frame with zero error should command exactly zero (no
    // leftover integral term from the prior large-error frame).
    const LiveFrameResult r =
        session.process_frame(wrap(make_frame_with_dot(320.0, 240.0), 0), test_source_info(), 0.02);
    CHECK(std::abs(r.command.pan_rate_rad_s) < 1e-9);
}

void test_invalid_arguments_rejected() {
    LiveTrackingSession session(make_config());
    const auto raw = wrap(make_frame_with_dot(320.0, 240.0), 0);
    CHECK_THROWS(session.process_frame(raw, test_source_info(), 0.0));
    CHECK_THROWS(session.process_frame(raw, test_source_info(), -1.0));
    fsoc::Frame empty{};
    CHECK_THROWS(session.process_frame(empty, test_source_info(), 0.02));
}

void test_ai_mode_requires_ai_detector_config() {
    LiveTrackingSessionConfig config = make_config();
    config.perception_mode = fsoc::PerceptionMode::Hybrid;
    // ai_detector left empty -- must throw, not construct a broken session.
    CHECK_THROWS(LiveTrackingSession{config});
}

}  // namespace

int main() {
    test_quadrant_signs();
    test_no_target_no_ground_truth_leak();
    test_control_disabled_never_commands();
    test_control_enabled_commands_toward_target();
    test_tracker_wiring_coasts_through_brief_gap();
    test_reset_clears_controller_and_actuator_state();
    test_invalid_arguments_rejected();
    test_ai_mode_requires_ai_detector_config();

    if (failures == 0) {
        std::cout << "PASS: 8 LiveTrackingSession checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
