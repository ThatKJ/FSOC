// Step 7 deterministic unit checks: the closed-loop SimulationRunner.
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/step6_tests.cpp.
// Links fsoc::simulation (which brings core/render/perception/control + OpenCV).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include "fsoc/config.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/observation.hpp"
#include "fsoc/pid_controller.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/tracking_error.hpp"
#include "fsoc/trajectory.hpp"

namespace {

using fsoc::deg_to_rad;
using fsoc::evaluate;
using fsoc::rad_to_deg;
using fsoc::SimulationMetrics;
using fsoc::SimulationRunner;
using fsoc::SimulationRunnerConfig;
using fsoc::SimulationStepResult;
using fsoc::total_angular_error_rad;
using fsoc::Vec3;

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
void check_throws_invalid_argument(Fn&& fn, const std::string_view expression, const int line) {
    bool threw = false;
    try {
        std::forward<Fn>(fn)();
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {
        // wrong exception type is still a failure
    }
    check(threw, expression, line);
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_NEAR(actual, expected, tol) \
    check_near((actual), (expected), (tol), #actual " ~= " #expected, __LINE__)
#define CHECK_THROWS_INVALID(expr) \
    check_throws_invalid_argument([&] { (void)(expr); }, #expr, __LINE__)

// ---- helpers -----------------------------------------------------

[[nodiscard]] double pixel_error_norm(const SimulationStepResult& r) {
    return r.tracking_error.has_value()
               ? std::hypot(r.tracking_error->pixel.x_px, r.tracking_error->pixel.y_px)
               : std::nan("");
}

[[nodiscard]] double angular_error_deg(const SimulationStepResult& r) {
    return r.tracking_error.has_value()
               ? rad_to_deg(total_angular_error_rad(*r.tracking_error))
               : std::nan("");
}

// ---- 1 / 2. clock: starts at 0, increments by exactly dt ----------

void test_clock_starts_and_increments() {
    const auto cfg = fsoc::baseline_runner_config();
    const fsoc::StationaryTrajectory target{Vec3{100.0, 5.0, 3.0}};
    SimulationRunner runner{cfg, target};

    CHECK(runner.simulation_time_s() == 0.0);
    CHECK(runner.frame_index() == 0);

    const auto results = runner.run_for(1.0);
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(results[i].frame_index == i);
        CHECK_NEAR(results[i].simulation_time_s,
                   static_cast<double>(i) * cfg.timestep_s, 1e-9);
    }
    CHECK_NEAR(runner.simulation_time_s(),
               static_cast<double>(results.size()) * cfg.timestep_s, 1e-9);
}

// ---- 3. determinism -------------------------------------------

void test_deterministic_sequence() {
    const auto cfg = fsoc::baseline_runner_config();
    fsoc::SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 10.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.2, 0.15};
    const fsoc::SinusoidalTrajectory target{p};

    SimulationRunner a{cfg, target};
    SimulationRunner b{cfg, target};
    const auto ra = a.run_for(6.0);
    const auto rb = b.run_for(6.0);
    CHECK(ra.size() == rb.size());
    for (std::size_t i = 0; i < ra.size(); ++i) {
        CHECK(ra[i].camera_pan_rad == rb[i].camera_pan_rad);
        CHECK(ra[i].camera_tilt_rad == rb[i].camera_tilt_rad);
        CHECK(ra[i].command.pan_rate_rad_s == rb[i].command.pan_rate_rad_s);
        CHECK(ra[i].command.tilt_rate_rad_s == rb[i].command.tilt_rate_rad_s);
        CHECK(ra[i].detection.has_value() == rb[i].detection.has_value());
        if (ra[i].detection.has_value() && rb[i].detection.has_value()) {
            CHECK(ra[i].detection->centroid_px.x_px == rb[i].detection->centroid_px.x_px);
            CHECK(ra[i].detection->centroid_px.y_px == rb[i].detection->centroid_px.y_px);
        }
    }
}

// ---- 4..10. static acquisition -------------------------------

void test_static_acquisition() {
    const auto cfg = fsoc::baseline_runner_config();
    const fsoc::StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};  // RIGHT + ABOVE
    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(5.0);
    const SimulationMetrics m = evaluate(results);

    // (5) detected through renderer -> detector, and every frame stays detected.
    CHECK(results.front().detection.has_value());
    CHECK(m.detected_frames == m.frame_count);

    // (4) begins clearly off-centre.
    CHECK(pixel_error_norm(results.front()) > 50.0);

    // (6) error decreases substantially.
    CHECK(pixel_error_norm(results.back()) < 0.02 * pixel_error_norm(results.front()));

    // (7) final angular error meets the < 0.05 deg gate.
    CHECK(angular_error_deg(results.back()) < 0.05);

    // (8) final centroid within 2 px of image centre.
    CHECK(pixel_error_norm(results.back()) < 2.0);

    // (9) target is RIGHT -> camera pan increased toward +ideal (~3.43 deg).
    CHECK(results.back().camera_pan_rad > results.front().camera_pan_rad);
    CHECK(results.back().camera_pan_rad > deg_to_rad(3.0));

    // (10) target is ABOVE -> camera tilt increased toward +ideal (~2.29 deg).
    CHECK(results.back().camera_tilt_rad > results.front().camera_tilt_rad);
    CHECK(results.back().camera_tilt_rad > deg_to_rad(2.0));

    // camera never below the actuator / PID limits.
    for (const auto& r : results) {
        CHECK(std::abs(r.command.pan_rate_rad_s) <= cfg.controller.pan.output_limit_rad_s + 1e-12);
        CHECK(std::abs(r.command.tilt_rate_rad_s) <= cfg.controller.tilt.output_limit_rad_s + 1e-12);
        CHECK(std::abs(r.applied_rates.pan_rate_rad_s) <= cfg.camera.max_pan_rate_rad_s + 1e-12);
        CHECK(std::abs(r.applied_rates.tilt_rate_rad_s) <= cfg.camera.max_tilt_rate_rad_s + 1e-12);
    }
}

// ---- 11 / 12. linear moving target -------------------------

void test_linear_tracking() {
    const auto cfg = fsoc::baseline_runner_config();
    const fsoc::LinearTrajectory target{Vec3{100.0, -8.0, -3.0}, Vec3{0.0, 2.0, 0.8}};
    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(10.0);
    const SimulationMetrics m = evaluate(results);

    CHECK(m.detection_fraction >= 0.95);                       // (11)
    CHECK(rad_to_deg(m.rms_angular_error_rad) < 0.75);         // (12) documented bound
    // steady-state (last 2 s) error is small and not growing.
    CHECK(angular_error_deg(results.back()) < 0.30);
}

// ---- 13 / 14. sinusoidal moving target -------------------

void test_sinusoidal_tracking() {
    const auto cfg = fsoc::baseline_runner_config();
    SimulationRunnerConfig sin_cfg = cfg;
    sin_cfg.initial_tilt_rad = std::atan2(3.0, 100.0);
    fsoc::SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 22.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.12, 0.09};
    const fsoc::SinusoidalTrajectory target{p};

    SimulationRunner runner{sin_cfg, target};
    const auto results = runner.run_for(20.0);
    const SimulationMetrics m = evaluate(results);

    CHECK(m.detection_fraction >= 0.95);                        // (13)
    CHECK(rad_to_deg(m.rms_angular_error_rad) < 1.0);           // (14) documented bound
    CHECK(rad_to_deg(m.max_angular_error_rad) < 1.5);           // no uncontrolled oscillation

    // Not diverging: RMS of the last 5 s <= RMS of the first 5 s after acquisition.
    auto window_rms_deg = [](const std::vector<SimulationStepResult>& r, std::size_t a,
                             std::size_t b) {
        double s = 0.0;
        std::size_t n = 0;
        for (std::size_t i = a; i < b && i < r.size(); ++i) {
            if (r[i].tracking_error.has_value()) {
                const double e = rad_to_deg(total_angular_error_rad(*r[i].tracking_error));
                s += e * e;
                ++n;
            }
        }
        return n > 0 ? std::sqrt(s / static_cast<double>(n)) : 0.0;
    };
    const double early = window_rms_deg(results, 100, 350);   // ~2..7 s
    const double late = window_rms_deg(results, 750, 1000);   // ~15..20 s
    CHECK(late <= early * 1.5);
}

// ---- 15. closed-loop outperforms open-loop -----------------

void test_closed_beats_open() {
    const auto cfg = fsoc::baseline_runner_config();
    SimulationRunnerConfig open_cfg = cfg;
    open_cfg.initial_tilt_rad = std::atan2(3.0, 100.0);
    open_cfg.control_enabled = false;

    fsoc::SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 22.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.12, 0.09};
    const fsoc::SinusoidalTrajectory target{p};

    SimulationRunner open_runner{open_cfg, target};
    const SimulationMetrics open_m = evaluate(open_runner.run_for(20.0));

    SimulationRunnerConfig closed_cfg = open_cfg;
    closed_cfg.control_enabled = true;
    SimulationRunner closed_runner{closed_cfg, target};
    const SimulationMetrics closed_m = evaluate(closed_runner.run_for(20.0));

    CHECK(closed_m.detection_fraction > open_m.detection_fraction);
    CHECK(closed_m.rms_angular_error_rad < open_m.rms_angular_error_rad);
    // The effect must be large, not marginal.
    CHECK(closed_m.detection_fraction - open_m.detection_fraction > 0.20);
    CHECK(open_m.rms_angular_error_rad > 3.0 * closed_m.rms_angular_error_rad);
}

// ---- 16. no-detection path: reset + zero command + camera holds ----

void test_no_detection_path() {
    const auto cfg = fsoc::baseline_runner_config();
    // Target far outside the field of view.
    const fsoc::StationaryTrajectory target{Vec3{100.0, 100.0, 0.0}};
    SimulationRunner runner{cfg, target};

    const auto results = runner.run_for(0.5);
    for (const auto& r : results) {
        CHECK(!r.detection.has_value());
        CHECK(!r.tracking_error.has_value());
        CHECK(r.command.pan_rate_rad_s == 0.0);
        CHECK(r.command.tilt_rate_rad_s == 0.0);
        CHECK(r.camera_pan_rad == cfg.initial_pan_rad);
        CHECK(r.camera_tilt_rad == cfg.initial_tilt_rad);
    }
    CHECK(runner.camera().pan_rad() == cfg.initial_pan_rad);
    CHECK(runner.camera().tilt_rad() == cfg.initial_tilt_rad);
}

// ---- 17. reappearance after loss: PID resumes from reset ---------

void test_reappearance_after_loss() {
    auto cfg = fsoc::baseline_runner_config();
    // A sinusoid fast enough that the actuator briefly falls behind and the
    // beacon slips out of the FOV, then the sinusoid reverses and it returns.
    fsoc::SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 42.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.30, 0.10};
    const fsoc::SinusoidalTrajectory target{p};
    cfg.initial_tilt_rad = std::atan2(3.0, 100.0);

    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(10.0);
    const SimulationMetrics m = evaluate(results);

    CHECK(m.lost_frames > 0);        // loss really happened
    CHECK(m.detected_frames > 0);    // and there is tracking too

    // There is a lost frame followed later by a detected frame (reacquisition).
    std::size_t last_lost = 0;
    bool any_lost = false;
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (!results[i].detection.has_value()) {
            last_lost = i;
            any_lost = true;
        }
    }
    CHECK(any_lost);
    CHECK(last_lost + 1 < results.size());
    bool reacquired = false;
    for (std::size_t i = last_lost + 1; i < results.size(); ++i) {
        if (results[i].detection.has_value()) {
            reacquired = true;
        }
    }
    CHECK(reacquired);

    // During any lost frame the command is zero and the pose is held.
    for (std::size_t i = 1; i < results.size(); ++i) {
        if (!results[i].detection.has_value()) {
            CHECK(results[i].command.pan_rate_rad_s == 0.0);
            CHECK(results[i].command.tilt_rate_rad_s == 0.0);
            if (!results[i - 1].detection.has_value()) {
                CHECK(results[i].camera_pan_rad == results[i - 1].camera_pan_rad);
                CHECK(results[i].camera_tilt_rad == results[i - 1].camera_tilt_rad);
            }
        }
    }
}

// ---- tracker seam: bit-identical when disabled + real closed-loop bridging --
//
// A minimal, deterministic trajectory that sits at `in_fov_position_m` for all
// time except a single closed [dropout_start_s, dropout_end_s) window, during
// which it reports `out_of_fov_position_m` -- an engineered, exact-length
// detection dropout (as opposed to test_reappearance_after_loss's incidental,
// multi-frame sinusoid loss) so the tracker's coast horizon and confidence
// decay can be exercised at precise frame boundaries.
class BriefDropoutTrajectory final : public fsoc::Trajectory {
public:
    BriefDropoutTrajectory(
        const Vec3 in_fov_position_m,
        const Vec3 out_of_fov_position_m,
        const double dropout_start_s,
        const double dropout_end_s)
        : in_position_(in_fov_position_m),
          out_position_(out_of_fov_position_m),
          dropout_start_s_(dropout_start_s),
          dropout_end_s_(dropout_end_s) {}

    [[nodiscard]] fsoc::TargetState state_at(const double time_s) const override {
        fsoc::TargetState state{};
        state.position_m =
            (time_s >= dropout_start_s_ && time_s < dropout_end_s_) ? out_position_ : in_position_;
        state.velocity_mps = Vec3{0.0, 0.0, 0.0};
        return state;
    }

private:
    Vec3 in_position_;
    Vec3 out_position_;
    double dropout_start_s_;
    double dropout_end_s_;
};

void test_tracker_disabled_is_bit_identical_to_baseline() {
    const fsoc::SimulationRunnerConfig cfg_plain = fsoc::baseline_runner_config();

    // Same config, tracker_enabled left false, but every tracker knob is
    // deliberately set to conspicuously non-default values. If disabling the
    // tracker is a genuine no-op (the seam it must be), these values must
    // never be read.
    fsoc::SimulationRunnerConfig cfg_with_unused_tracker = cfg_plain;
    cfg_with_unused_tracker.tracker.alpha = 0.9;
    cfg_with_unused_tracker.tracker.beta = 0.9;
    cfg_with_unused_tracker.tracker.outlier_gate_px = 1.0;
    cfg_with_unused_tracker.tracker.max_coast_frames = 0;
    cfg_with_unused_tracker.tracker_min_confidence_to_steer = 0.99;

    fsoc::SinusoidalTrajectory::Parameters p{};
    p.center_position_m = Vec3{100.0, 0.0, 3.0};
    p.amplitude_m = Vec3{0.0, 42.0, 4.0};
    p.frequency_hz = Vec3{0.0, 0.30, 0.10};
    const fsoc::SinusoidalTrajectory target{p};

    SimulationRunner a{cfg_plain, target};
    SimulationRunner b{cfg_with_unused_tracker, target};
    const auto ra = a.run_for(5.0);
    const auto rb = b.run_for(5.0);

    CHECK(ra.size() == rb.size());
    for (std::size_t i = 0; i < ra.size(); ++i) {
        CHECK(ra[i].detection.has_value() == rb[i].detection.has_value());
        if (ra[i].detection.has_value() && rb[i].detection.has_value()) {
            CHECK(ra[i].detection->centroid_px.x_px == rb[i].detection->centroid_px.x_px);
            CHECK(ra[i].detection->centroid_px.y_px == rb[i].detection->centroid_px.y_px);
        }
        CHECK(ra[i].camera_pan_rad == rb[i].camera_pan_rad);
        CHECK(ra[i].camera_tilt_rad == rb[i].camera_tilt_rad);
        CHECK(ra[i].command.pan_rate_rad_s == rb[i].command.pan_rate_rad_s);
        CHECK(ra[i].command.tilt_rate_rad_s == rb[i].command.tilt_rate_rad_s);
        // The tracker must never have been constructed/run: its output stays
        // default-constructed (LockState::Searching) for the entire run.
        CHECK(rb[i].tracked_state.lock_state == fsoc::LockState::Searching);
    }
}

void test_tracker_bridges_one_frame_dropout_in_closed_loop() {
    auto cfg = fsoc::baseline_runner_config();
    cfg.tracker_enabled = true;  // TargetTrackerConfig{} defaults throughout

    const Vec3 in_pos{100.0, 0.0, 0.0};    // centered on the default camera axis
    const Vec3 out_pos{100.0, 500.0, 0.0};  // far outside the narrow FOV
    const double dt = cfg.timestep_s;
    // Exactly one frame (index 20) reports out_pos; every other frame reports
    // in_pos.
    const BriefDropoutTrajectory target{in_pos, out_pos, 20.0 * dt, 21.0 * dt};

    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(1.0);

    // Acquisition: the tracker needs acquire_frames_required (3) consecutive
    // real measurements before Tracking; by the frame right before the
    // dropout it must be an established track.
    CHECK(results[19].tracked_state.lock_state == fsoc::LockState::Tracking);
    CHECK(results[19].detection.has_value());

    // The dropout frame: classical perception sees nothing (target truly
    // outside the FOV -- verified against the tracker-disabled baseline
    // below), but the tracker bridges it by prediction. Confidence after one
    // coast frame is 1.0 * confidence_decay_per_coast_frame (0.5) = 0.5,
    // which is >= the default tracker_min_confidence_to_steer (0.4): safe to
    // steer, so the control-facing detection stays populated.
    CHECK(results[20].tracked_state.lock_state == fsoc::LockState::Coasting);
    CHECK(results[20].tracked_state.is_prediction);
    CHECK(results[20].tracked_state.coast_frames == 1);
    CHECK_NEAR(results[20].tracked_state.confidence, 0.5, 1e-9);
    CHECK(results[20].detection.has_value());
    CHECK_NEAR(results[20].detection->centroid_px.x_px, results[19].detection->centroid_px.x_px, 1e-6);
    CHECK_NEAR(results[20].detection->centroid_px.y_px, results[19].detection->centroid_px.y_px, 1e-6);

    // Reacquisition: the very next frame's real measurement lands well inside
    // the outlier gate (the target never actually moved), so the track goes
    // straight back to Tracking -- no re-acquisition delay after a bridged gap.
    CHECK(results[21].tracked_state.lock_state == fsoc::LockState::Tracking);
    CHECK(results[21].detection.has_value());

    // Cross-check against the SAME scenario with the tracker disabled: the
    // classical/hybrid perception layer genuinely lost the target for that
    // one frame -- the tracker is bridging a real gap, not fabricating one.
    fsoc::SimulationRunnerConfig cfg_no_tracker = cfg;
    cfg_no_tracker.tracker_enabled = false;
    SimulationRunner baseline{cfg_no_tracker, target};
    const auto baseline_results = baseline.run_for(1.0);
    CHECK(!baseline_results[20].detection.has_value());
    CHECK(baseline_results[19].detection.has_value());
    CHECK(baseline_results[21].detection.has_value());
}

void test_tracker_two_frame_dropout_exercises_confidence_gate_in_closed_loop() {
    auto cfg = fsoc::baseline_runner_config();
    cfg.tracker_enabled = true;

    const Vec3 in_pos{100.0, 0.0, 0.0};
    const Vec3 out_pos{100.0, 500.0, 0.0};
    const double dt = cfg.timestep_s;
    // Exactly two consecutive frames (20, 21) report out_pos -- still within
    // max_coast_frames (2), so the tracker stays Coasting (not Lost)
    // throughout, but the SECOND coast frame's decayed confidence
    // (1.0 * 0.5 * 0.5 = 0.25) drops below tracker_min_confidence_to_steer
    // (0.4): is_safe_to_steer must go false even though the tracker itself
    // has not declared the track lost. This is the deliberate distinction
    // between "still tracked" (TargetTracker's own bookkeeping) and "safe to
    // steer" (the separate, explicit control-safety policy) -- see
    // include/fsoc/target_tracker.hpp.
    const BriefDropoutTrajectory target{in_pos, out_pos, 20.0 * dt, 22.0 * dt};

    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(1.0);

    CHECK(results[19].tracked_state.lock_state == fsoc::LockState::Tracking);

    CHECK(results[20].tracked_state.lock_state == fsoc::LockState::Coasting);
    CHECK(results[20].tracked_state.coast_frames == 1);
    CHECK_NEAR(results[20].tracked_state.confidence, 0.5, 1e-9);
    CHECK(results[20].detection.has_value());  // first coast frame: safe to steer

    CHECK(results[21].tracked_state.lock_state == fsoc::LockState::Coasting);
    CHECK(results[21].tracked_state.coast_frames == 2);
    CHECK_NEAR(results[21].tracked_state.confidence, 0.25, 1e-9);
    CHECK(!results[21].detection.has_value());  // second coast frame: NOT safe to steer

    // The track itself survived (never Lost) and reacquires cleanly.
    CHECK(results[22].tracked_state.lock_state == fsoc::LockState::Tracking);
    CHECK(results[22].detection.has_value());
}

// ---- demo disturbance seam (Phase J): bit-identical when disabled, real
//      effect when enabled -----------------------------------------------

void test_disturbance_disabled_is_bit_identical_to_baseline() {
    const fsoc::SimulationRunnerConfig cfg_plain = fsoc::baseline_runner_config();

    fsoc::SimulationRunnerConfig cfg_with_unused_disturbance = cfg_plain;
    cfg_with_unused_disturbance.disturbance.kind = fsoc::DemoDisturbanceKind::None;
    cfg_with_unused_disturbance.disturbance.noise_sigma = 40.0;
    cfg_with_unused_disturbance.disturbance.distractor_peak = 255.0;
    cfg_with_unused_disturbance.disturbance.distractor_sigma_px = 8.0;

    const fsoc::StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};
    SimulationRunner a{cfg_plain, target};
    SimulationRunner b{cfg_with_unused_disturbance, target};
    const auto ra = a.run_for(2.0);
    const auto rb = b.run_for(2.0);

    CHECK(ra.size() == rb.size());
    for (std::size_t i = 0; i < ra.size(); ++i) {
        CHECK(ra[i].detection.has_value() == rb[i].detection.has_value());
        if (ra[i].detection.has_value() && rb[i].detection.has_value()) {
            CHECK(ra[i].detection->centroid_px.x_px == rb[i].detection->centroid_px.x_px);
            CHECK(ra[i].detection->centroid_px.y_px == rb[i].detection->centroid_px.y_px);
        }
        CHECK(ra[i].camera_pan_rad == rb[i].camera_pan_rad);
        CHECK(ra[i].camera_tilt_rad == rb[i].camera_tilt_rad);
    }
}

void test_disturbance_noise_is_deterministic_and_changes_the_frame() {
    fsoc::SimulationRunnerConfig cfg = fsoc::baseline_runner_config();
    cfg.disturbance.kind = fsoc::DemoDisturbanceKind::Noise;
    cfg.disturbance.noise_sigma = 40.0;

    const fsoc::StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};
    SimulationRunner clean_runner{fsoc::baseline_runner_config(), target};
    SimulationRunner noisy_a{cfg, target};
    SimulationRunner noisy_b{cfg, target};

    const auto clean = clean_runner.run_for(0.1);
    const auto a = noisy_a.run_for(0.1);
    const auto b = noisy_b.run_for(0.1);

    bool any_pixel_differs_from_clean = false;
    bool a_and_b_identical = true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].rendered_frame.size() == clean[i].rendered_frame.size());
        if (cv::countNonZero(a[i].rendered_frame != clean[i].rendered_frame) > 0) {
            any_pixel_differs_from_clean = true;
        }
        if (cv::countNonZero(a[i].rendered_frame != b[i].rendered_frame) > 0) {
            a_and_b_identical = false;
        }
    }
    CHECK(any_pixel_differs_from_clean);  // the disturbance actually changed the frame
    CHECK(a_and_b_identical);             // same config + trajectory -> byte-identical (deterministic)
}

void test_disturbance_clutter_can_cause_false_lock() {
    fsoc::SimulationRunnerConfig cfg = fsoc::baseline_runner_config();
    cfg.disturbance.kind = fsoc::DemoDisturbanceKind::Clutter;
    cfg.disturbance.distractor_peak = 255.0;
    cfg.disturbance.distractor_sigma_px = 6.0;  // larger integrated signal than the beacon (sigma=2.0)

    const fsoc::StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};
    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(2.0);

    // A distractor with more integrated signal than the beacon must pull the
    // classical "brightest component" centroid away from the true projection
    // on at least one frame -- if it never did, this seam would be dead code.
    bool saw_large_detection_error = false;
    for (const auto& r : results) {
        if (r.detection_error_px.has_value() && *r.detection_error_px > 20.0) {
            saw_large_detection_error = true;
        }
    }
    CHECK(saw_large_detection_error);
}

void test_disturbance_occlusion_erases_beacon_in_exact_window() {
    fsoc::SimulationRunnerConfig cfg = fsoc::baseline_runner_config();
    cfg.disturbance.kind = fsoc::DemoDisturbanceKind::Occlusion;
    cfg.disturbance.occlusion_start_frame = 20;
    cfg.disturbance.occlusion_duration_frames = 2;

    const fsoc::StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};
    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(1.0);

    CHECK(results[19].detection.has_value());
    CHECK(!results[20].detection.has_value());  // occluded
    CHECK(!results[21].detection.has_value());  // occluded
    CHECK(results[22].detection.has_value());   // window closed, beacon reappears
}

void test_disturbance_invalid_config_rejected() {
    fsoc::SimulationRunnerConfig cfg = fsoc::baseline_runner_config();
    cfg.disturbance.kind = fsoc::DemoDisturbanceKind::Noise;
    cfg.disturbance.noise_sigma = -1.0;
    CHECK_THROWS_INVALID(cfg.validate());

    fsoc::SimulationRunnerConfig cfg2 = fsoc::baseline_runner_config();
    cfg2.disturbance.kind = fsoc::DemoDisturbanceKind::Clutter;
    cfg2.disturbance.distractor_peak = 300.0;
    CHECK_THROWS_INVALID(cfg2.validate());
}

// ---- 18 / 19. command and applied-rate limits (checked broadly) --

void test_rate_limits_respected() {
    const auto cfg = fsoc::baseline_runner_config();
    // Deliberately harsh: big initial offset -> commands saturate for a while.
    const fsoc::LinearTrajectory target{Vec3{100.0, 9.0, 6.0}, Vec3{0.0, 3.0, 1.5}};
    SimulationRunner runner{cfg, target};
    const auto results = runner.run_for(8.0);

    bool saw_saturation = false;
    for (const auto& r : results) {
        CHECK(std::abs(r.command.pan_rate_rad_s) <= cfg.controller.pan.output_limit_rad_s + 1e-12);
        CHECK(std::abs(r.command.tilt_rate_rad_s) <= cfg.controller.tilt.output_limit_rad_s + 1e-12);
        CHECK(std::abs(r.applied_rates.pan_rate_rad_s) <= cfg.camera.max_pan_rate_rad_s + 1e-12);
        CHECK(std::abs(r.applied_rates.tilt_rate_rad_s) <= cfg.camera.max_tilt_rate_rad_s + 1e-12);
        if (std::abs(r.command.pan_rate_rad_s) >= cfg.controller.pan.output_limit_rad_s - 1e-9) {
            saw_saturation = true;
        }
    }
    CHECK(saw_saturation);  // the harsh start really did hit the limit
}

// ---- 20 + truth-shortcut: control follows DETECTED pixels ---------

void test_control_follows_detected_not_truth() {
    // Build the exact control chain by hand with a frame whose DETECTED centroid
    // is on the OPPOSITE side of centre from the true projection.
    const fsoc::CameraConfig camera_config{};
    const fsoc::PanTiltCamera camera{camera_config};  // origin, pan/tilt 0

    // Truth target to the LEFT of the optical axis.
    const Vec3 truth_target_m{100.0, -6.0, 0.0};
    const fsoc::CameraObservation observation = fsoc::observe_beacon(camera, truth_target_m);
    CHECK(observation.visible());
    CHECK(observation.image_point_px.has_value());
    CHECK(observation.image_point_px->x_px < camera.cx_px());  // truth really is LEFT

    const fsoc::SyntheticCameraRenderer renderer{fsoc::renderer_config_for(camera_config, 2.0)};
    cv::Mat frame = renderer.render(observation);  // faint Gaussian beacon on the LEFT

    // Paint a much stronger blob to the RIGHT of centre.
    const int cx = static_cast<int>(camera.cx_px());
    const int cy = static_cast<int>(camera.cy_px());
    frame(cv::Rect(cx + 55, cy - 15, 32, 32)).setTo(cv::Scalar(255.0));

    const fsoc::BeaconDetector detector{fsoc::BeaconDetectorConfig{}};
    const std::optional<fsoc::BeaconDetection> detection = detector.detect(frame);
    CHECK(detection.has_value());
    CHECK(detection->centroid_px.x_px > camera.cx_px());  // detector picked the RIGHT blob

    const std::optional<fsoc::TrackingError> error =
        fsoc::compute_tracking_error(detection, camera);
    CHECK(error.has_value());
    CHECK(error->pixel.x_px > 0.0);          // error points RIGHT (toward the DETECTED blob)
    CHECK(error->angular.pan_rad > 0.0);

    fsoc::PIDController pid{fsoc::PIDControllerConfig{}};
    const fsoc::ControlCommand command = pid.update(*error, 0.02);
    // Control commands PAN RIGHT even though the true target is to the LEFT:
    // the loop is driven by detected pixels, not by projection truth.
    CHECK(command.pan_rate_rad_s > 0.0);
}

// ---- manual one-frame trace: pixel error DECREASES because camera moved ----

void test_manual_one_frame_trace() {
    const auto cfg = fsoc::baseline_runner_config();
    const fsoc::StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};  // RIGHT + ABOVE
    SimulationRunner runner{cfg, target};

    const SimulationStepResult r0 = runner.step();
    CHECK(r0.detection.has_value());
    CHECK(r0.tracking_error.has_value());
    CHECK(r0.tracking_error->pixel.x_px > 0.0);       // beacon RIGHT of centre
    CHECK(r0.tracking_error->pixel.y_px < 0.0);       // beacon ABOVE centre
    CHECK(r0.tracking_error->angular.pan_rad > 0.0);
    CHECK(r0.tracking_error->angular.tilt_rad > 0.0);
    CHECK(r0.command.pan_rate_rad_s > 0.0);           // PAN RIGHT
    CHECK(r0.command.tilt_rate_rad_s > 0.0);          // TILT UP

    // Camera actually moved in the commanded direction.
    CHECK(runner.camera().pan_rad() > r0.camera_pan_rad);
    CHECK(runner.camera().tilt_rad() > r0.camera_tilt_rad);

    // THE crucial requirement: next frame's pixel error is smaller.
    const SimulationStepResult r1 = runner.step();
    CHECK(r1.tracking_error.has_value());
    CHECK(pixel_error_norm(r1) < pixel_error_norm(r0));
}

// ---- P0 config-validation regression -----------------------------
//
// SimulationRunnerConfig is a plain C++20 struct: a MISSPELLED field name
// (the classic YAML/JSON "typo silently changes behaviour" bug) is not
// representable at all -- it is a compile error. The real equivalent failure
// mode in this architecture is a *correctly-named* field holding an
// out-of-range or wrong-unit VALUE that used to be silently coerced instead
// of rejected -- e.g. initial_tilt_rad outside the camera's mechanical limits
// used to reach PanTiltCamera's constructor and get silently std::clamp()'d,
// quietly starting the scenario at the wrong attitude. This test demonstrates
// that class of bug is now caught before the simulation starts.

void test_out_of_range_initial_tilt_rejected() {
    fsoc::SimulationRunnerConfig cfg = fsoc::baseline_runner_config();
    const fsoc::StationaryTrajectory target{Vec3{100.0, 0.0, 0.0}};

    // Camera default tilt limits are +/-80 deg (fsoc/config.hpp). 150 deg is a
    // realistic "meant degrees, unit/typo mistake" out-of-range value that
    // PRE-FIX would have been silently clamped to +80 deg instead of rejected.
    cfg.initial_tilt_rad = deg_to_rad(150.0);
    CHECK_THROWS_INVALID(cfg.validate());
    CHECK_THROWS_INVALID(SimulationRunner(cfg, target));

    // Symmetric check on the other side of the range.
    fsoc::SimulationRunnerConfig cfg_neg = fsoc::baseline_runner_config();
    cfg_neg.initial_tilt_rad = deg_to_rad(-150.0);
    CHECK_THROWS_INVALID(cfg_neg.validate());

    // A boundary-valid tilt (exactly at the limit) must NOT throw.
    fsoc::SimulationRunnerConfig cfg_boundary = fsoc::baseline_runner_config();
    cfg_boundary.initial_tilt_rad = cfg_boundary.camera.max_tilt_rad;
    bool boundary_ok = true;
    try {
        cfg_boundary.validate();
        const SimulationRunner runner{cfg_boundary, target};
        (void)runner;
    } catch (...) {
        boundary_ok = false;
    }
    CHECK(boundary_ok);
}

void test_non_finite_initial_pose_rejected() {
    const fsoc::StationaryTrajectory target{Vec3{100.0, 0.0, 0.0}};
    const double nan_value = std::nan("");

    fsoc::SimulationRunnerConfig bad_pan = fsoc::baseline_runner_config();
    bad_pan.initial_pan_rad = nan_value;
    CHECK_THROWS_INVALID(bad_pan.validate());

    fsoc::SimulationRunnerConfig bad_tilt = fsoc::baseline_runner_config();
    bad_tilt.initial_tilt_rad = nan_value;
    CHECK_THROWS_INVALID(bad_tilt.validate());

    fsoc::SimulationRunnerConfig bad_position = fsoc::baseline_runner_config();
    bad_position.camera_position_m = Vec3{nan_value, 0.0, 0.0};
    CHECK_THROWS_INVALID(bad_position.validate());
    CHECK_THROWS_INVALID(SimulationRunner(bad_position, target));
}

// ---- 21..26. prior steps still green (behavioural spot checks) ----

void test_prior_steps_regression() {
    const fsoc::PanTiltCamera camera{fsoc::CameraConfig{}};

    // Step 1: projection centre.
    const auto centre = camera.project({100.0, 0.0, 0.0});
    CHECK(centre.has_value());
    CHECK_NEAR(centre->u_px, camera.cx_px(), 1e-9);

    // Step 2: trajectory.
    const fsoc::LinearTrajectory lin{{100.0, 0.0, 2.0}, {0.0, 1.0, 0.2}};
    CHECK_NEAR(lin.state_at(2.0).position_m.z, 2.4, 1e-12);

    // Step 3: observation classification.
    CHECK(fsoc::observe_beacon(camera, {-10.0, 0.0, 0.0}).status ==
          fsoc::ObservationStatus::BehindCamera);

    // Step 4/5: render -> detect a centred beacon.
    const fsoc::SyntheticCameraRenderer renderer{fsoc::renderer_config_for(fsoc::CameraConfig{}, 2.0)};
    const cv::Mat frame = renderer.render(fsoc::CameraObservation{
        .status = fsoc::ObservationStatus::Visible,
        .image_point_px = fsoc::ImagePoint{.x_px = camera.cx_px(), .y_px = camera.cy_px()}});
    const fsoc::BeaconDetector detector{fsoc::BeaconDetectorConfig{}};
    const auto det = detector.detect(frame);
    CHECK(det.has_value());
    CHECK_NEAR(det->centroid_px.x_px, camera.cx_px(), 0.1);

    // Step 6: PID sign.
    fsoc::PIDController pid{fsoc::PIDControllerConfig{}};
    const auto cmd = pid.update([] {
        fsoc::TrackingError e{};
        e.angular.pan_rad = 0.05;
        e.angular.tilt_rad = 0.02;
        return e;
    }(), 0.02);
    CHECK(cmd.pan_rate_rad_s > 0.0);
    CHECK(cmd.tilt_rate_rad_s > 0.0);
}

}  // namespace

int main() {
    test_clock_starts_and_increments();
    test_deterministic_sequence();
    test_static_acquisition();
    test_linear_tracking();
    test_sinusoidal_tracking();
    test_closed_beats_open();
    test_no_detection_path();
    test_reappearance_after_loss();
    test_tracker_disabled_is_bit_identical_to_baseline();
    test_tracker_bridges_one_frame_dropout_in_closed_loop();
    test_tracker_two_frame_dropout_exercises_confidence_gate_in_closed_loop();
    test_disturbance_disabled_is_bit_identical_to_baseline();
    test_disturbance_noise_is_deterministic_and_changes_the_frame();
    test_disturbance_clutter_can_cause_false_lock();
    test_disturbance_occlusion_erases_beacon_in_exact_window();
    test_disturbance_invalid_config_rejected();
    test_rate_limits_respected();
    test_control_follows_detected_not_truth();
    test_manual_one_frame_trace();
    test_out_of_range_initial_tilt_rejected();
    test_non_finite_initial_pose_rejected();
    test_prior_steps_regression();

    if (failures == 0) {
        std::cout << "PASS: 22 Step-7 closed-loop checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
