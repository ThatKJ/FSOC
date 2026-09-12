// Mobile Phone Camera-in-the-Loop milestone: VirtualPanTiltActuator unit
// checks. Same lightweight harness as tests/step1_tests.cpp .. tests/step6_tests.cpp.
// OpenCV-free — links fsoc::core only (config.hpp is header-only).

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "fsoc/config.hpp"
#include "fsoc/virtual_actuator.hpp"

namespace {

using fsoc::VirtualActuatorConfig;
using fsoc::VirtualPanTiltActuator;

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
    } catch (const std::invalid_argument&) {
        // expected
    } catch (...) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << " threw the wrong exception type\n";
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_NEAR(actual, expected, tol) check_near((actual), (expected), (tol), #actual, __LINE__)
#define CHECK_THROWS(expr) check_throws([&] { (void)(expr); }, #expr, __LINE__)

void test_config_validation() {
    VirtualActuatorConfig ok{};
    ok.validate();  // must not throw

    VirtualActuatorConfig bad_pan{};
    bad_pan.max_pan_rate_rad_s = 0.0;
    CHECK_THROWS(bad_pan.validate());

    VirtualActuatorConfig bad_tilt{};
    bad_tilt.max_tilt_rate_rad_s = -1.0;
    CHECK_THROWS(bad_tilt.validate());

    VirtualActuatorConfig bad_range{};
    bad_range.min_tilt_rad = 1.0;
    bad_range.max_tilt_rad = 0.5;
    CHECK_THROWS(bad_range.validate());
}

void test_integration_and_zero_start() {
    VirtualPanTiltActuator actuator{};
    CHECK_NEAR(actuator.state().pan_rad, 0.0, 1e-12);
    CHECK_NEAR(actuator.state().tilt_rad, 0.0, 1e-12);

    const auto s = actuator.step(fsoc::deg_to_rad(10.0), fsoc::deg_to_rad(-5.0), 1.0);
    CHECK_NEAR(fsoc::rad_to_deg(s.pan_rad), 10.0, 1e-9);
    CHECK_NEAR(fsoc::rad_to_deg(s.tilt_rad), -5.0, 1e-9);
    CHECK(!s.pan_saturated);
    CHECK(!s.tilt_saturated);
}

void test_rate_saturation() {
    VirtualActuatorConfig cfg{};
    cfg.max_pan_rate_rad_s = fsoc::deg_to_rad(30.0);
    cfg.max_tilt_rate_rad_s = fsoc::deg_to_rad(30.0);
    VirtualPanTiltActuator actuator(cfg);

    const auto s = actuator.step(fsoc::deg_to_rad(100.0), fsoc::deg_to_rad(-100.0), 1.0);
    CHECK(s.pan_saturated);
    CHECK(s.tilt_saturated);
    CHECK_NEAR(fsoc::rad_to_deg(s.pan_rate_rad_s), 30.0, 1e-9);
    CHECK_NEAR(fsoc::rad_to_deg(s.tilt_rate_rad_s), -30.0, 1e-9);
    CHECK_NEAR(fsoc::rad_to_deg(s.pan_rad), 30.0, 1e-9);
}

void test_tilt_travel_limit() {
    VirtualActuatorConfig cfg{};
    cfg.min_tilt_rad = fsoc::deg_to_rad(-10.0);
    cfg.max_tilt_rad = fsoc::deg_to_rad(10.0);
    VirtualPanTiltActuator actuator(cfg);

    const auto s1 = actuator.step(0.0, fsoc::deg_to_rad(30.0), 1.0);  // would reach 30deg, clamp to 10
    CHECK_NEAR(fsoc::rad_to_deg(s1.tilt_rad), 10.0, 1e-9);
    CHECK_NEAR(s1.tilt_rate_rad_s, 0.0, 1e-12);
}

void test_reset() {
    VirtualPanTiltActuator actuator{};
    (void)actuator.step(fsoc::deg_to_rad(10.0), fsoc::deg_to_rad(10.0), 1.0);
    actuator.reset();
    CHECK_NEAR(actuator.state().pan_rad, 0.0, 1e-12);
    CHECK_NEAR(actuator.state().tilt_rad, 0.0, 1e-12);
    CHECK(!actuator.state().pan_saturated);
}

void test_invalid_step_arguments() {
    VirtualPanTiltActuator actuator{};
    CHECK_THROWS(actuator.step(0.0, 0.0, 0.0));
    CHECK_THROWS(actuator.step(0.0, 0.0, -1.0));
    CHECK_THROWS(actuator.step(std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0));
}

}  // namespace

int main() {
    test_config_validation();
    test_integration_and_zero_start();
    test_rate_saturation();
    test_tilt_travel_limit();
    test_reset();
    test_invalid_step_arguments();

    if (failures == 0) {
        std::cout << "PASS: 6 VirtualPanTiltActuator checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
