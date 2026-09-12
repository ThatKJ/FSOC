// Mobile Phone Camera-in-the-Loop milestone: FrameSource / OpenCVCameraFrameSource
// unit checks. Same lightweight harness as tests/step1_tests.cpp .. tests/step6_tests.cpp.
//
// Deliberately does NOT open a real, valid camera device (no hardware access
// from an automated test run — see docs/PHONE_CAMERA_METRICS.md). Only checks
// config validation and the clean-failure path for an out-of-range index,
// which touches no real device.

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "fsoc/opencv_camera_frame_source.hpp"

namespace {

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

void test_to_string() {
    CHECK(std::string(fsoc::to_string(fsoc::FrameSourceKind::Synthetic)) == "SYNTHETIC");
    CHECK(std::string(fsoc::to_string(fsoc::FrameSourceKind::OpenCVCamera)) == "REAL_CAMERA");
}

void test_config_requires_exactly_one_source() {
    fsoc::OpenCVCameraFrameSourceConfig neither{};
    CHECK_THROWS(neither.validate());

    fsoc::OpenCVCameraFrameSourceConfig both{};
    both.camera_index = 0;
    both.url = "http://example.invalid/stream";
    CHECK_THROWS(both.validate());

    fsoc::OpenCVCameraFrameSourceConfig index_only{};
    index_only.camera_index = 0;
    index_only.validate();  // must not throw

    fsoc::OpenCVCameraFrameSourceConfig url_only{};
    url_only.url = "http://example.invalid/stream";
    url_only.validate();  // must not throw
}

void test_out_of_range_index_fails_cleanly() {
    // A very high index does not correspond to any real device on any CI
    // machine or developer laptop -- open() must return false, never throw,
    // never hang, and never touch a real camera.
    fsoc::OpenCVCameraFrameSourceConfig config{};
    config.camera_index = 9999;
    fsoc::OpenCVCameraFrameSource source(config);
    CHECK(!source.open());
    fsoc::Frame frame{};
    CHECK(!source.read(frame));  // never opened -- read must also fail cleanly
    source.close();              // must not throw even though never opened
}

}  // namespace

int main() {
    test_to_string();
    test_config_requires_exactly_one_source();
    test_out_of_range_index_fails_cleanly();

    if (failures == 0) {
        std::cout << "PASS: 3 FrameSource checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
