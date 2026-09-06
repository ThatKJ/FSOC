// Step 9 deterministic unit checks: engineering camera-view visualization.
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/step8_tests.cpp.
// Links fsoc::visualization (transitively simulation/telemetry/render/... + OpenCV).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "fsoc/config.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/telemetry.hpp"
#include "fsoc/trajectory.hpp"
#include "fsoc/visualization.hpp"

namespace {

using fsoc::SimulationStepResult;
using fsoc::TelemetryRecord;
using fsoc::TrackingState;
using fsoc::TrackingVisualizer;
using fsoc::VisualizationConfig;

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

template <typename Fn>
void check_throws(Fn&& fn, const std::string_view expression, const int line) {
    bool threw = false;
    try {
        std::forward<Fn>(fn)();
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {
    }
    check(threw, expression, line);
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_NEAR(actual, expected, tol) \
    check_near((actual), (expected), (tol), #actual " ~= " #expected, __LINE__)
#define CHECK_THROWS(expr) check_throws([&] { (void)(expr); }, #expr, __LINE__)

// ---- helpers ---------------------------------------------------------

constexpr int kBg = 5;  // renderer background level -> cvtColor makes (5,5,5)

[[nodiscard]] cv::Mat gray_frame(const int w = 640, const int h = 480, const int value = kBg) {
    return cv::Mat(h, w, CV_8UC1, cv::Scalar(static_cast<double>(value)));
}

[[nodiscard]] bool images_equal(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size() || a.type() != b.type()) {
        return false;
    }
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    return cv::countNonZero(diff.reshape(1)) == 0;
}

[[nodiscard]] int count_non_bg(const cv::Mat& bgr, const cv::Rect& roi, const int bg = kBg) {
    const cv::Rect r = roi & cv::Rect(0, 0, bgr.cols, bgr.rows);
    int n = 0;
    for (int y = r.y; y < r.y + r.height; ++y) {
        for (int x = r.x; x < r.x + r.width; ++x) {
            const cv::Vec3b p = bgr.at<cv::Vec3b>(y, x);
            if (p[0] != bg || p[1] != bg || p[2] != bg) {
                ++n;
            }
        }
    }
    return n;
}

[[nodiscard]] cv::Rect around(const int cx, const int cy, const int half) {
    return cv::Rect(cx - half, cy - half, 2 * half, 2 * half);
}

[[nodiscard]] TelemetryRecord tracking_telemetry() {
    TelemetryRecord t{};
    t.simulation_time_s = 1.50;
    t.frame_index = 75;
    t.target_visible = true;
    t.target_detected = true;
    t.detected_x_px = 400.0;
    t.detected_y_px = 180.0;
    t.pixel_error_x_px = 80.0;
    t.pixel_error_y_px = -60.0;
    t.angular_error_pan_rad = 0.044;
    t.angular_error_tilt_rad = 0.033;
    t.angular_error_total_rad = std::hypot(0.044, 0.033);
    t.camera_pan_rad = 0.10;
    t.camera_tilt_rad = 0.05;
    t.command_pan_rate_rad_s = 0.30;
    t.command_tilt_rate_rad_s = -0.10;
    t.applied_pan_rate_rad_s = 0.30;
    t.applied_tilt_rate_rad_s = -0.10;
    t.pan_saturated = false;
    t.tilt_saturated = false;
    t.detection_error_px = 0.012;
    t.tracking_state = TrackingState::Tracking;
    return t;
}

[[nodiscard]] TelemetryRecord lost_telemetry() {
    TelemetryRecord t{};
    t.simulation_time_s = 2.00;
    t.frame_index = 100;
    t.target_visible = false;
    t.target_detected = false;
    t.camera_pan_rad = 0.20;
    t.camera_tilt_rad = 0.03;
    t.tracking_state = TrackingState::TargetLost;
    return t;
}

[[nodiscard]] SimulationStepResult tracking_result() {
    SimulationStepResult r{};
    r.simulation_time_s = 1.50;
    r.frame_index = 75;
    r.target_visible = true;
    r.target_detected = true;
    r.observation.status = fsoc::ObservationStatus::Visible;
    r.observation.image_point_px = fsoc::ImagePoint{.x_px = 399.0, .y_px = 181.0};  // truth
    r.detection = fsoc::BeaconDetection{.centroid_px = {400.0, 180.0}};
    fsoc::TrackingError e{};
    e.pixel = {80.0, -60.0};
    e.angular = {0.044, 0.033};
    r.tracking_error = e;
    r.command = {0.30, -0.10};
    r.applied_rates = {0.30, -0.10};
    r.camera_pan_rad = 0.10;
    r.camera_tilt_rad = 0.05;
    r.detection_error_px = 0.012;
    return r;
}

[[nodiscard]] SimulationStepResult lost_result() {
    SimulationStepResult r{};
    r.simulation_time_s = 2.00;
    r.frame_index = 100;
    r.observation.status = fsoc::ObservationStatus::OutsideFieldOfView;
    r.camera_pan_rad = 0.20;
    r.camera_tilt_rad = 0.03;
    return r;
}

[[nodiscard]] VisualizationConfig only(void (*setter)(VisualizationConfig&)) {
    VisualizationConfig cfg{};
    cfg.show_title = false;
    cfg.show_crosshair = false;
    cfg.show_detection_marker = false;
    cfg.show_error_vector = false;
    cfg.show_status = false;
    cfg.show_sim_time = false;
    cfg.show_camera_attitude = false;
    cfg.show_angular_error = false;
    cfg.show_pixel_error = false;
    cfg.show_command_rates = false;
    cfg.show_detection_error = false;
    cfg.show_truth_marker = false;
    setter(cfg);
    return cfg;
}

// ---- 1..5. output format + input immutability + validation ---------

void test_output_format_and_input_immutable() {
    const TrackingVisualizer viz{VisualizationConfig{}};
    const cv::Mat in = gray_frame(640, 480);
    const cv::Mat in_copy = in.clone();

    const cv::Mat out = viz.annotate(in, tracking_result(), tracking_telemetry());
    CHECK(out.type() == CV_8UC3);            // (1)
    CHECK(out.channels() == 3);
    CHECK(out.size() == in.size());          // (2)
    CHECK(images_equal(in, in_copy));        // (3) input untouched, byte-for-byte

    // (4) wrong type / (5) empty rejected.
    CHECK_THROWS(viz.annotate(cv::Mat(480, 640, CV_8UC3, cv::Scalar(5, 5, 5)),
                              tracking_result(), tracking_telemetry()));
    CHECK_THROWS(viz.annotate(cv::Mat(480, 640, CV_32FC1, cv::Scalar(0.5)), tracking_result(),
                              tracking_telemetry()));
    CHECK_THROWS(viz.annotate(cv::Mat{}, tracking_result(), tracking_telemetry()));
}

// ---- 6. crosshair at the geometric image centre, not hardcoded ----

void test_crosshair_centre() {
    const TrackingVisualizer viz{only([](VisualizationConfig& c) { c.show_crosshair = true; })};

    const cv::Mat out = viz.annotate(gray_frame(640, 480), lost_result(), lost_telemetry());
    CHECK(count_non_bg(out, around(320, 240, 18)) > 0);   // crosshair drew near (320,240)
    CHECK(count_non_bg(out, around(60, 60, 20)) == 0);    // nothing else anywhere

    // Different frame size -> centre moves; not hardcoded to 320/240.
    const cv::Mat big = viz.annotate(gray_frame(800, 600), lost_result(), lost_telemetry());
    CHECK(count_non_bg(big, around(400, 300, 18)) > 0);
    CHECK(count_non_bg(big, around(320, 240, 8)) == 0);   // old centre is now background
}

// ---- 7. detection marker at the DETECTED coordinate ---------------

void test_detection_marker_position() {
    const TrackingVisualizer viz{
        only([](VisualizationConfig& c) { c.show_detection_marker = true; })};
    TelemetryRecord t = tracking_telemetry();
    t.detected_x_px = 450.0;
    t.detected_y_px = 150.0;
    SimulationStepResult r = tracking_result();
    r.observation.image_point_px = fsoc::ImagePoint{.x_px = 200.0, .y_px = 350.0};  // truth elsewhere

    const cv::Mat out = viz.annotate(gray_frame(), r, t);
    CHECK(count_non_bg(out, around(450, 150, 12)) > 0);   // marker at DETECTED
    CHECK(count_non_bg(out, around(200, 350, 12)) == 0);  // NOT at truth projection
    CHECK(count_non_bg(out, around(320, 240, 8)) == 0);   // NOT at image centre
}

// ---- 8. no detection marker when the target is lost --------------

void test_no_marker_when_lost() {
    const TrackingVisualizer viz{
        only([](VisualizationConfig& c) { c.show_detection_marker = true; })};

    TelemetryRecord t = tracking_telemetry();
    t.detected_x_px = 450.0;
    t.detected_y_px = 150.0;
    const cv::Mat tracked = viz.annotate(gray_frame(), tracking_result(), t);
    CHECK(count_non_bg(tracked, around(450, 150, 12)) > 0);

    const cv::Mat lost = viz.annotate(gray_frame(), lost_result(), lost_telemetry());
    CHECK(count_non_bg(lost, cv::Rect(0, 0, lost.cols, lost.rows)) == 0);  // nothing drawn at all
}

// ---- 9 / 10. error vector present iff a TrackingError exists ------

void test_error_vector_presence() {
    const TrackingVisualizer viz{
        only([](VisualizationConfig& c) { c.show_error_vector = true; })};
    TelemetryRecord t = tracking_telemetry();
    t.detected_x_px = 450.0;
    t.detected_y_px = 150.0;

    // midpoint of the vector centre(320,240) -> (450,150) is ~ (385, 195)
    const cv::Mat tracked = viz.annotate(gray_frame(), tracking_result(), t);
    CHECK(count_non_bg(tracked, around(385, 195, 6)) > 0);   // (9)

    const cv::Mat lost = viz.annotate(gray_frame(), lost_result(), lost_telemetry());
    CHECK(count_non_bg(lost, around(385, 195, 6)) == 0);     // (10)
}

// ---- 11. tracking-state indicator differs Tracking vs TargetLost --

void test_status_indicator_differs() {
    const TrackingVisualizer viz{only([](VisualizationConfig& c) { c.show_status = true; })};
    const cv::Mat a = viz.annotate(gray_frame(), tracking_result(), tracking_telemetry());
    const cv::Mat b = viz.annotate(gray_frame(), lost_result(), lost_telemetry());
    CHECK(!images_equal(a, b));
    // the difference is in the top-left status block
    const cv::Rect tl(0, 0, 260, 90);
    CHECK(!images_equal(a(tl).clone(), b(tl).clone()));
}

// ---- 12. saturation indicator only when the flag is set ---------

void test_saturation_indicator() {
    const TrackingVisualizer hud{
        only([](VisualizationConfig& c) { c.show_command_rates = true; })};
    TelemetryRecord t = tracking_telemetry();
    TelemetryRecord t_sat = t;
    t_sat.pan_saturated = true;
    CHECK(!images_equal(hud.annotate(gray_frame(), tracking_result(), t),
                        hud.annotate(gray_frame(), tracking_result(), t_sat)));

    // With no HUD at all, the saturation flag alone draws nothing.
    const TrackingVisualizer none{only([](VisualizationConfig&) {})};
    CHECK(images_equal(none.annotate(gray_frame(), tracking_result(), t),
                       none.annotate(gray_frame(), tracking_result(), t_sat)));
}

// ---- 13. PAN/TILT HUD reflects telemetry values ----------------

void test_attitude_hud_uses_telemetry() {
    const TrackingVisualizer viz{
        only([](VisualizationConfig& c) { c.show_camera_attitude = true; })};
    TelemetryRecord a = tracking_telemetry();
    TelemetryRecord b = a;
    b.camera_pan_rad = 0.50;  // different attitude
    CHECK(!images_equal(viz.annotate(gray_frame(), tracking_result(), a),
                        viz.annotate(gray_frame(), tracking_result(), b)));
}

// ---- 14. simulation time / frame index are represented --------

void test_sim_time_represented() {
    const TrackingVisualizer viz{only([](VisualizationConfig& c) { c.show_sim_time = true; })};
    TelemetryRecord a = tracking_telemetry();
    TelemetryRecord b = a;
    b.simulation_time_s = 9.876;
    b.frame_index = 493;
    CHECK(!images_equal(viz.annotate(gray_frame(), tracking_result(), a),
                        viz.annotate(gray_frame(), tracking_result(), b)));

    const TrackingVisualizer none{only([](VisualizationConfig&) {})};
    CHECK(images_equal(none.annotate(gray_frame(), tracking_result(), a),
                       none.annotate(gray_frame(), tracking_result(), b)));
}

// ---- 15. deterministic: same inputs -> byte-identical output ----

void test_deterministic_annotation() {
    const TrackingVisualizer viz{VisualizationConfig{}};
    const cv::Mat a = viz.annotate(gray_frame(), tracking_result(), tracking_telemetry());
    const cv::Mat b = viz.annotate(gray_frame(), tracking_result(), tracking_telemetry());
    CHECK(images_equal(a, b));
}

// ---- 16. every overlay can be disabled -> plain BGR conversion --

void test_all_overlays_disabled() {
    const TrackingVisualizer viz{only([](VisualizationConfig&) {})};  // all flags false
    const cv::Mat gray = gray_frame();
    const cv::Mat out = viz.annotate(gray, tracking_result(), tracking_telemetry());
    cv::Mat plain;
    cv::cvtColor(gray, plain, cv::COLOR_GRAY2BGR);
    CHECK(images_equal(out, plain));
}

// ---- 17. truth marker: off by default, distinct when enabled ---

void test_truth_marker_policy() {
    CHECK(VisualizationConfig{}.show_truth_marker == false);

    SimulationStepResult r = tracking_result();
    r.observation.image_point_px = fsoc::ImagePoint{.x_px = 200.0, .y_px = 350.0};

    const TrackingVisualizer defaulted{VisualizationConfig{}};
    CHECK(count_non_bg(defaulted.annotate(gray_frame(), r, tracking_telemetry()),
                       around(200, 350, 12)) == 0);  // nothing at truth by default

    VisualizationConfig cfg{};
    cfg.show_truth_marker = true;
    const TrackingVisualizer with_truth{cfg};
    CHECK(count_non_bg(with_truth.annotate(gray_frame(), r, tracking_telemetry()),
                       around(200, 350, 12)) > 0);  // square marker appears when enabled
}

// ---- 18. annotate() does not mutate its inputs ----------------

void test_inputs_not_mutated() {
    const TrackingVisualizer viz{VisualizationConfig{}};
    const SimulationStepResult r = tracking_result();
    const TelemetryRecord t = tracking_telemetry();
    SimulationStepResult r_copy = r;
    TelemetryRecord t_copy = t;

    (void)viz.annotate(gray_frame(), r, t);

    CHECK(r.frame_index == r_copy.frame_index);
    CHECK(r.camera_pan_rad == r_copy.camera_pan_rad);
    CHECK(r.detection.has_value() == r_copy.detection.has_value());
    CHECK(r.tracking_error.has_value() == r_copy.tracking_error.has_value());
    CHECK(t.frame_index == t_copy.frame_index);
    CHECK(t.camera_pan_rad == t_copy.camera_pan_rad);
    CHECK(t.detected_x_px == t_copy.detected_x_px);
    CHECK(t.angular_error_total_rad == t_copy.angular_error_total_rad);
    CHECK(t.tracking_state == t_copy.tracking_state);
}

// ---- P0: rendered_frame IS the detector's exact input, not a lookalike ----
//
// Proves SimulationStepResult::rendered_frame is not merely "a frame that
// happens to look the same" but is bit-for-bit the pixels detector_.detect()
// actually ran on: re-running the SAME classical detector on the stored frame
// reproduces the SAME centroid the runner already reported. This is the
// property TrackingVisualizer / evidence export now rely on instead of
// re-rendering from `observation`.

void test_rendered_frame_is_detector_input() {
    const auto cfg = fsoc::baseline_runner_config();
    const fsoc::StationaryTrajectory target{fsoc::Vec3{100.0, 5.0, 2.0}};
    fsoc::SimulationRunner runner{cfg, target};
    const fsoc::BeaconDetector detector{cfg.detector};

    for (int i = 0; i < 10; ++i) {
        const SimulationStepResult r = runner.step();
        CHECK(!r.rendered_frame.empty());
        CHECK(r.rendered_frame.type() == CV_8UC1);

        const auto redetected = detector.detect(r.rendered_frame);
        CHECK(redetected.has_value() == r.detection.has_value());
        if (redetected.has_value() && r.detection.has_value()) {
            CHECK(redetected->centroid_px.x_px == r.detection->centroid_px.x_px);
            CHECK(redetected->centroid_px.y_px == r.detection->centroid_px.y_px);
        }
    }
}

// ---- 19. MANDATORY: visualization does not perturb the simulation --

void test_visualization_non_interference() {
    const auto cfg = fsoc::baseline_runner_config();
    fsoc::SinusoidalTrajectory::Parameters p{};
    p.center_position_m = {100.0, 0.0, 3.0};
    p.amplitude_m = {0.0, 22.0, 4.0};
    p.frequency_hz = {0.0, 0.12, 0.09};
    const fsoc::SinusoidalTrajectory target{p};
    fsoc::SimulationRunnerConfig sin_cfg = cfg;
    sin_cfg.initial_tilt_rad = std::atan2(3.0, 100.0);

    // A: plain closed loop.
    fsoc::SimulationRunner runner_a{sin_cfg, target};
    std::vector<SimulationStepResult> a;
    for (int i = 0; i < 500; ++i) {
        a.push_back(runner_a.step());
    }

    // B: same run, annotate the EXACT frame the detector consumed each step
    // (SimulationStepResult::rendered_frame) — not a fresh re-render. A
    // same-step cross-check against a fresh render is kept below as a
    // trip-wire: it is only guaranteed to hold while rendering stays a pure,
    // noise-free function of the observation, and will need attention the
    // moment disturbance work breaks that guarantee.
    const fsoc::SyntheticCameraRenderer renderer{sin_cfg.renderer};
    const TrackingVisualizer viz{VisualizationConfig{}};
    fsoc::SimulationRunner runner_b{sin_cfg, target};
    std::vector<SimulationStepResult> b;
    for (int i = 0; i < 500; ++i) {
        const SimulationStepResult r = runner_b.step();
        b.push_back(r);
        const cv::Mat& base = r.rendered_frame;
        const cv::Mat base_copy = base.clone();
        CHECK(images_equal(base, renderer.render(r.observation)));  // trip-wire, see comment above
        const TelemetryRecord telem = fsoc::make_telemetry_record(
            r, sin_cfg.camera.max_pan_rate_rad_s, sin_cfg.camera.max_tilt_rate_rad_s);
        const cv::Mat display = viz.annotate(base, r, telem);
        CHECK(display.type() == CV_8UC3);
        CHECK(images_equal(base, base_copy));  // annotate never touched the base frame
    }

    CHECK(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].simulation_time_s == b[i].simulation_time_s);
        CHECK(a[i].camera_pan_rad == b[i].camera_pan_rad);
        CHECK(a[i].camera_tilt_rad == b[i].camera_tilt_rad);
        CHECK(a[i].command.pan_rate_rad_s == b[i].command.pan_rate_rad_s);
        CHECK(a[i].command.tilt_rate_rad_s == b[i].command.tilt_rate_rad_s);
        CHECK(a[i].applied_rates.pan_rate_rad_s == b[i].applied_rates.pan_rate_rad_s);
        CHECK(a[i].target_detected == b[i].target_detected);
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

// ---- 20..27. prior steps remain green (spot checks) -------------

void test_prior_steps_regression() {
    const fsoc::PanTiltCamera camera{fsoc::CameraConfig{}};
    const auto centre = camera.project({100.0, 0.0, 0.0});
    CHECK(centre.has_value());
    CHECK_NEAR(centre->u_px, camera.cx_px(), 1e-9);

    const fsoc::LinearTrajectory lin{{100.0, 0.0, 2.0}, {0.0, 1.0, 0.2}};
    CHECK_NEAR(lin.state_at(2.0).position_m.z, 2.4, 1e-12);

    CHECK(fsoc::observe_beacon(camera, {-10.0, 0.0, 0.0}).status ==
          fsoc::ObservationStatus::BehindCamera);

    const fsoc::SyntheticCameraRenderer renderer{fsoc::renderer_config_for(fsoc::CameraConfig{}, 2.0)};
    const cv::Mat frame = renderer.render(fsoc::CameraObservation{
        .status = fsoc::ObservationStatus::Visible,
        .image_point_px = fsoc::ImagePoint{.x_px = camera.cx_px(), .y_px = camera.cy_px()}});
    const fsoc::BeaconDetector detector{fsoc::BeaconDetectorConfig{}};
    const auto det = detector.detect(frame);
    CHECK(det.has_value());
    CHECK_NEAR(det->centroid_px.x_px, camera.cx_px(), 0.1);

    fsoc::PIDController pid{fsoc::PIDControllerConfig{}};
    fsoc::TrackingError e{};
    e.angular.pan_rad = 0.05;
    e.angular.tilt_rad = 0.02;
    const auto cmd = pid.update(e, 0.02);
    CHECK(cmd.pan_rate_rad_s > 0.0 && cmd.tilt_rate_rad_s > 0.0);

    // Step 7 runner + Step 8 telemetry still work end to end.
    const fsoc::StationaryTrajectory st{fsoc::Vec3{100.0, 6.0, 4.0}};
    fsoc::SimulationRunner runner{fsoc::baseline_runner_config(), st};
    const fsoc::RecordedRun run = fsoc::run_and_record(runner, 3.0);
    const fsoc::BenchmarkMetrics bm =
        fsoc::compute_benchmark_metrics(run.telemetry, run.wall_execution_time_s);
    CHECK(bm.frames == 150);
    CHECK(fsoc::rad_to_deg(bm.final_angular_error_rad) < 0.05);
}

}  // namespace

int main() {
    test_output_format_and_input_immutable();
    test_crosshair_centre();
    test_detection_marker_position();
    test_no_marker_when_lost();
    test_error_vector_presence();
    test_status_indicator_differs();
    test_saturation_indicator();
    test_attitude_hud_uses_telemetry();
    test_sim_time_represented();
    test_deterministic_annotation();
    test_all_overlays_disabled();
    test_truth_marker_policy();
    test_inputs_not_mutated();
    test_rendered_frame_is_detector_input();
    test_visualization_non_interference();
    test_prior_steps_regression();

    if (failures == 0) {
        std::cout << "PASS: 16 Step-9 visualization checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
