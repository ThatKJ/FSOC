// P0-v2 deterministic unit checks: TargetTracker (alpha-beta state estimator +
// short-horizon coasting + temporal-consistency gate).
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/step6_tests.cpp.
// OpenCV-free — links fsoc::core only.

#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "fsoc/image_geometry.hpp"
#include "fsoc/target_tracker.hpp"

namespace {

using fsoc::ImagePoint;
using fsoc::LockState;
using fsoc::TargetTracker;
using fsoc::TargetTrackerConfig;
using fsoc::TrackedState;

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
void check_throws_invalid_argument(Fn&& fn, const std::string_view expression, const int line) {
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
#define CHECK_THROWS_INVALID(expr) \
    check_throws_invalid_argument([&] { (void)(expr); }, #expr, __LINE__)

constexpr double kDt = 0.02;  // 50 Hz, matches the simulation's fixed timestep

[[nodiscard]] ImagePoint pt(const double x, const double y) { return ImagePoint{.x_px = x, .y_px = y}; }

// ---- 1. first measurement is handled correctly (Acquiring, not a jump from 0,0) ----

void test_first_measurement() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 3}};
    const TrackedState s = tracker.update(pt(320.0, 240.0), kDt);
    CHECK(s.lock_state == LockState::Acquiring);
    CHECK_NEAR(s.x_px, 320.0, 1e-9);
    CHECK_NEAR(s.y_px, 240.0, 1e-9);
    CHECK_NEAR(s.vx_px_s, 0.0, 1e-9);
    CHECK_NEAR(s.vy_px_s, 0.0, 1e-9);
    CHECK(!s.is_prediction);
    CHECK_NEAR(s.confidence, 1.0, 1e-9);
}

// ---- 2. acquire_frames_required consecutive detections -> Tracking ----

void test_acquisition_sequence() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 3}};
    CHECK(tracker.update(pt(100, 100), kDt).lock_state == LockState::Acquiring);
    CHECK(tracker.update(pt(100, 100), kDt).lock_state == LockState::Acquiring);
    CHECK(tracker.update(pt(100, 100), kDt).lock_state == LockState::Tracking);
}

// ---- 2b. acquisition-consistency gate (P0-v2, MVP V2 Phase E): a run of
//          spatially INCONSISTENT candidates must never confirm a track --
//          each implausible jump restarts acquisition fresh instead of
//          counting toward acquire_frames_required. Without this, Classical's
//          brightest-blob rule false-locking onto a different random clutter
//          position every frame (docs/MVP_ABLATION.md Phase E,
//          stage4_degradation.cpp apply_clutter) would confirm a garbage
//          track just as readily as a real, consistent one.

void test_acquisition_restarts_on_spatially_inconsistent_measurements() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 3, .outlier_gate_px = 60.0}};

    const TrackedState s1 = tracker.update(pt(50.0, 50.0), kDt);
    CHECK(s1.lock_state == LockState::Acquiring);
    CHECK_NEAR(s1.x_px, 50.0, 1e-6);

    // > outlier_gate_px away -- must restart acquisition, not accumulate.
    const TrackedState s2 = tracker.update(pt(500.0, 400.0), kDt);
    CHECK(s2.lock_state == LockState::Acquiring);
    CHECK_NEAR(s2.x_px, 500.0, 1e-6);  // snapped to the NEW measurement, not blended toward the old one
    CHECK_NEAR(s2.vx_px_s, 0.0, 1e-9);  // velocity reset on restart, exactly like a fresh acquisition

    const TrackedState s3 = tracker.update(pt(10.0, 10.0), kDt);  // inconsistent again -- restarts again
    CHECK(s3.lock_state == LockState::Acquiring);
    CHECK_NEAR(s3.x_px, 10.0, 1e-6);

    // Three mutually CONSISTENT, closely-spaced measurements now confirm
    // Tracking normally (the first of the three still restarts acquisition,
    // since it is > 60 px from the last inconsistent anchor at (10,10)).
    CHECK(tracker.update(pt(300.0, 200.0), kDt).lock_state == LockState::Acquiring);
    CHECK(tracker.update(pt(305.0, 202.0), kDt).lock_state == LockState::Acquiring);
    CHECK(tracker.update(pt(308.0, 199.0), kDt).lock_state == LockState::Tracking);
}

// ---- 3. stationary target: converges to the measured position, ~zero velocity ----

void test_stationary_target() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 1}};
    TrackedState s{};
    for (int i = 0; i < 20; ++i) {
        s = tracker.update(pt(300.0, 200.0), kDt);
    }
    CHECK(s.lock_state == LockState::Tracking);
    CHECK_NEAR(s.x_px, 300.0, 1e-6);
    CHECK_NEAR(s.y_px, 200.0, 1e-6);
    CHECK_NEAR(s.vx_px_s, 0.0, 1e-6);
    CHECK_NEAR(s.vy_px_s, 0.0, 1e-6);
}

// ---- 4. constant velocity: the filter learns the true velocity ----

void test_constant_velocity() {
    TargetTracker tracker{TargetTrackerConfig{.alpha = 0.6, .beta = 0.3, .acquire_frames_required = 1}};
    const double true_vx = 50.0;  // px/s
    const double true_vy = -20.0;
    double x = 100.0;
    double y = 400.0;
    TrackedState s{};
    for (int i = 0; i < 200; ++i) {  // let the filter settle
        s = tracker.update(pt(x, y), kDt);
        x += true_vx * kDt;
        y += true_vy * kDt;
    }
    CHECK(s.lock_state == LockState::Tracking);
    CHECK_NEAR(s.vx_px_s, true_vx, 1.0);   // settled within 1 px/s of truth
    CHECK_NEAR(s.vy_px_s, true_vy, 1.0);
}

// ---- 5. measurement noise is smoothed, not passed straight through ----

void test_noise_is_smoothed() {
    TargetTracker tracker{TargetTrackerConfig{.alpha = 0.3, .beta = 0.1, .acquire_frames_required = 1}};
    // Alternating +/-5 px noise around a stationary target: the smoothed
    // estimate must swing far less than the raw measurement does. The very
    // first frame legitimately snaps straight to the first measurement (no
    // prior exists to smooth from -- see the "fresh acquisition" contract),
    // so swing is measured starting from the second frame onward.
    const TrackedState first = tracker.update(pt(305.0, 240.0), kDt);
    double max_estimate_swing = 0.0;
    double prev = first.x_px;
    for (int i = 1; i < 40; ++i) {
        const double noisy_x = 300.0 + ((i % 2 == 0) ? 5.0 : -5.0);
        const TrackedState s = tracker.update(pt(noisy_x, 240.0), kDt);
        max_estimate_swing = std::max(max_estimate_swing, std::abs(s.x_px - prev));
        prev = s.x_px;
    }
    CHECK(max_estimate_swing < 5.0);  // raw measurement swings by 10 px/step; estimate must not
}

// ---- 6. irregular dt: velocity uses the ACTUAL dt, not an assumed fixed one ----

void test_irregular_dt() {
    TargetTracker tracker{TargetTrackerConfig{.alpha = 1.0, .beta = 1.0, .acquire_frames_required = 1}};
    // alpha=beta=1.0 makes this an exact (no-smoothing) constant-velocity fit,
    // so the resulting velocity estimate must exactly match true velocity
    // regardless of dt varying step to step.
    (void)tracker.update(pt(0.0, 0.0), 0.02);
    const TrackedState s2 = tracker.update(pt(10.0, 0.0), 0.05);  // moved 10px over 50ms -> 200 px/s
    CHECK_NEAR(s2.vx_px_s, 200.0, 1e-6);
    const TrackedState s3 = tracker.update(pt(10.0 + 4.0, 0.0), 0.01);  // +4px over 10ms -> 400 px/s innovation
    // predicted x before this update = 10 + 200*0.01 = 12; measurement=14; dx=2; dt=0.01 -> beta*dx/dt=200 added
    CHECK(s3.lock_state == LockState::Tracking);
}

// ---- 7. invalid dt is rejected, state unchanged ----

void test_invalid_dt_rejected() {
    TargetTracker tracker{};
    const TrackedState before = tracker.update(pt(1.0, 1.0), kDt);
    CHECK_THROWS_INVALID(tracker.update(pt(2.0, 2.0), 0.0));
    CHECK_THROWS_INVALID(tracker.update(pt(2.0, 2.0), -0.02));
    CHECK_THROWS_INVALID(tracker.update(pt(2.0, 2.0), std::nan("")));
    const TrackedState after = tracker.state();
    CHECK_NEAR(before.x_px, after.x_px, 1e-12);
    CHECK_NEAR(before.y_px, after.y_px, 1e-12);
    CHECK(before.lock_state == after.lock_state);
}

// ---- 8. reset() returns to a clean Searching state ----

void test_reset() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 1}};
    (void)tracker.update(pt(50.0, 60.0), kDt);
    (void)tracker.update(pt(55.0, 60.0), kDt);
    CHECK(tracker.state().lock_state == LockState::Tracking);
    tracker.reset();
    CHECK(tracker.state().lock_state == LockState::Searching);
    CHECK_NEAR(tracker.state().x_px, 0.0, 1e-12);
    CHECK_NEAR(tracker.state().confidence, 0.0, 1e-12);
    // Post-reset, a fresh measurement behaves exactly like the very first ever
    // (acquire_frames_required=1 here, so it confirms as Tracking immediately).
    const TrackedState s = tracker.update(pt(400.0, 100.0), kDt);
    CHECK(s.lock_state == LockState::Tracking);
    CHECK_NEAR(s.vx_px_s, 0.0, 1e-12);
}

// ---- 9. missing measurement while Tracking -> Coasting, predicted position ----

void test_coasting_bridges_short_gap() {
    TargetTracker tracker{
        TargetTrackerConfig{.alpha = 1.0, .beta = 1.0, .acquire_frames_required = 1, .max_coast_frames = 2}};
    (void)tracker.update(pt(0.0, 0.0), kDt);
    const TrackedState s2 = tracker.update(pt(2.0, 0.0), kDt);  // vx = 100 px/s
    CHECK(s2.lock_state == LockState::Tracking);

    const TrackedState coast1 = tracker.update(std::nullopt, kDt);
    CHECK(coast1.lock_state == LockState::Coasting);
    CHECK(coast1.is_prediction);
    CHECK_NEAR(coast1.x_px, 2.0 + 100.0 * kDt, 1e-9);  // predicted forward at last known velocity
    CHECK(coast1.confidence < 1.0);                     // decayed

    const TrackedState coast2 = tracker.update(std::nullopt, kDt);
    CHECK(coast2.lock_state == LockState::Coasting);
    CHECK(coast2.confidence < coast1.confidence);       // decays further

    // Real measurement returns before the horizon is exceeded -> reacquire smoothly.
    const TrackedState back = tracker.update(pt(4.2, 0.0), kDt);
    CHECK(back.lock_state == LockState::Tracking);
    CHECK(!back.is_prediction);
    CHECK_NEAR(back.confidence, 1.0, 1e-9);
    CHECK(back.coast_frames == 0);
}

// ---- 10. coast horizon exceeded -> Lost, then a fresh measurement re-Acquires ----

void test_coast_horizon_exceeded_goes_lost() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 1, .max_coast_frames = 2}};
    (void)tracker.update(pt(10.0, 10.0), kDt);
    (void)tracker.update(pt(11.0, 10.0), kDt);
    CHECK(tracker.update(std::nullopt, kDt).lock_state == LockState::Coasting);   // coast 1/2
    CHECK(tracker.update(std::nullopt, kDt).lock_state == LockState::Coasting);   // coast 2/2 (at horizon)
    const TrackedState lost = tracker.update(std::nullopt, kDt);                  // horizon exceeded
    CHECK(lost.lock_state == LockState::Lost);
    CHECK_NEAR(lost.confidence, 0.0, 1e-12);

    // A track that goes Lost does not linger -- the next call re-acquires
    // cleanly (acquire_frames_required=1 here, so it confirms immediately).
    const TrackedState reacquired = tracker.update(pt(500.0, 500.0), kDt);
    CHECK(reacquired.lock_state == LockState::Tracking);
    CHECK_NEAR(reacquired.vx_px_s, 0.0, 1e-12);  // no stale velocity carried over
}

// ---- 11. an unconfirmed (Acquiring) track that loses its candidate goes
//          straight to Searching -- it never coasts an unconfirmed track ----

void test_acquiring_track_lost_goes_to_searching_not_coasting() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 5}};
    (void)tracker.update(pt(1.0, 1.0), kDt);
    CHECK(tracker.state().lock_state == LockState::Acquiring);
    const TrackedState s = tracker.update(std::nullopt, kDt);
    CHECK(s.lock_state == LockState::Searching);
}

// ---- 12. large implausible jump on an established track is gated out ----
// (empirically justified: max legitimate frame-to-frame delta observed in the
// fastest validated Step-10 scenario is 6.07 px; 60 px default gate is a ~10x
// margin -- see target_tracker.hpp.)

void test_large_jump_outlier_rejected() {
    TargetTracker tracker{
        TargetTrackerConfig{.acquire_frames_required = 1, .max_coast_frames = 2, .outlier_gate_px = 60.0}};
    (void)tracker.update(pt(320.0, 240.0), kDt);
    (void)tracker.update(pt(320.0, 240.0), kDt);
    CHECK(tracker.state().lock_state == LockState::Tracking);

    // A "clutter" candidate 400 px away -- must be rejected, not accepted.
    const TrackedState s = tracker.update(pt(720.0, 240.0), kDt);
    CHECK(s.lock_state == LockState::Coasting);
    CHECK(s.measurement_rejected_outlier);
    CHECK_NEAR(s.x_px, 320.0, 1.0);  // stayed near the track, did NOT jump to the outlier
}

// ---- 13. a small, plausible jump (within the gate) is accepted normally ----

void test_small_jump_within_gate_accepted() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 1, .outlier_gate_px = 60.0}};
    (void)tracker.update(pt(320.0, 240.0), kDt);
    (void)tracker.update(pt(320.0, 240.0), kDt);
    const TrackedState s = tracker.update(pt(340.0, 240.0), kDt);  // 20 px jump, well inside the gate
    CHECK(s.lock_state == LockState::Tracking);
    CHECK(!s.measurement_rejected_outlier);
    CHECK(!s.is_prediction);
}

// ---- 14. no measurement at all while Searching stays Searching ----

void test_no_measurement_while_searching_stays_searching() {
    TargetTracker tracker{};
    CHECK(tracker.update(std::nullopt, kDt).lock_state == LockState::Searching);
    CHECK(tracker.update(std::nullopt, kDt).lock_state == LockState::Searching);
}

// ---- 15. invalid config is rejected at construction ----

void test_invalid_config_rejected() {
    CHECK_THROWS_INVALID(TargetTracker(TargetTrackerConfig{.alpha = 0.0}));
    CHECK_THROWS_INVALID(TargetTracker(TargetTrackerConfig{.alpha = 1.5}));
    CHECK_THROWS_INVALID(TargetTracker(TargetTrackerConfig{.beta = -0.1}));
    CHECK_THROWS_INVALID(TargetTracker(TargetTrackerConfig{.acquire_frames_required = 0}));
    CHECK_THROWS_INVALID(TargetTracker(TargetTrackerConfig{.max_coast_frames = -1}));
    CHECK_THROWS_INVALID(TargetTracker(TargetTrackerConfig{.confidence_decay_per_coast_frame = 1.5}));
    CHECK_THROWS_INVALID(TargetTracker(TargetTrackerConfig{.outlier_gate_px = 0.0}));
    bool ok = true;
    try {
        TargetTracker t{TargetTrackerConfig{}};
        (void)t;
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
}

// ---- 16. max_coast_frames = 0 disables coasting entirely ----

void test_zero_coast_frames_disables_coasting() {
    TargetTracker tracker{TargetTrackerConfig{.acquire_frames_required = 1, .max_coast_frames = 0}};
    (void)tracker.update(pt(0.0, 0.0), kDt);
    CHECK(tracker.state().lock_state == LockState::Tracking);
    const TrackedState s = tracker.update(std::nullopt, kDt);
    CHECK(s.lock_state == LockState::Lost);
}

// ---- 17. determinism: same input sequence from reset() -> identical output ----

void test_deterministic() {
    TargetTracker a{TargetTrackerConfig{.acquire_frames_required = 2}};
    TargetTracker b{TargetTrackerConfig{.acquire_frames_required = 2}};
    const double xs[] = {10.0, 12.0, 15.0, 15.5, 15.4, 20.0};
    for (const double x : xs) {
        const TrackedState sa = a.update(pt(x, 50.0), kDt);
        const TrackedState sb = b.update(pt(x, 50.0), kDt);
        CHECK(sa.x_px == sb.x_px);
        CHECK(sa.vx_px_s == sb.vx_px_s);
        CHECK(sa.lock_state == sb.lock_state);
    }
}

// ---- 18. is_safe_to_steer: the explicit control-safety policy ----

void test_is_safe_to_steer_policy() {
    TrackedState tracking{};
    tracking.lock_state = LockState::Tracking;
    CHECK(fsoc::is_safe_to_steer(tracking, 0.4));  // Tracking is always safe

    TrackedState coasting_confident{};
    coasting_confident.lock_state = LockState::Coasting;
    coasting_confident.confidence = 0.5;
    CHECK(fsoc::is_safe_to_steer(coasting_confident, 0.4));

    TrackedState coasting_stale{};
    coasting_stale.lock_state = LockState::Coasting;
    coasting_stale.confidence = 0.25;
    CHECK(!fsoc::is_safe_to_steer(coasting_stale, 0.4));

    TrackedState lost{};
    lost.lock_state = LockState::Lost;
    CHECK(!fsoc::is_safe_to_steer(lost, 0.0));  // never safe, even at a permissive threshold

    TrackedState searching{};
    searching.lock_state = LockState::Searching;
    CHECK(!fsoc::is_safe_to_steer(searching, 0.0));

    TrackedState acquiring{};
    acquiring.lock_state = LockState::Acquiring;
    acquiring.confidence = 1.0;
    CHECK(!fsoc::is_safe_to_steer(acquiring, 0.0));  // unconfirmed track is never safe to predict from
}

}  // namespace

int main() {
    test_first_measurement();
    test_acquisition_sequence();
    test_acquisition_restarts_on_spatially_inconsistent_measurements();
    test_stationary_target();
    test_constant_velocity();
    test_noise_is_smoothed();
    test_irregular_dt();
    test_invalid_dt_rejected();
    test_reset();
    test_coasting_bridges_short_gap();
    test_coast_horizon_exceeded_goes_lost();
    test_acquiring_track_lost_goes_to_searching_not_coasting();
    test_large_jump_outlier_rejected();
    test_small_jump_within_gate_accepted();
    test_no_measurement_while_searching_stays_searching();
    test_invalid_config_rejected();
    test_zero_coast_frames_disables_coasting();
    test_deterministic();
    test_is_safe_to_steer_policy();

    if (failures == 0) {
        std::cout << "PASS: 19 TargetTracker checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
