// Mobile Phone Camera-in-the-Loop milestone: preprocess_live_frame() unit
// checks. Same lightweight harness as tests/step1_tests.cpp .. tests/step6_tests.cpp.

#include <iostream>
#include <stdexcept>
#include <string_view>

#include <opencv2/core.hpp>

#include "fsoc/live_preprocessing.hpp"

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

void test_grayscale_passthrough_resized() {
    const cv::Mat raw(480, 640, CV_8UC1, cv::Scalar(100));
    const cv::Mat out = fsoc::preprocess_live_frame(raw);
    CHECK(out.type() == CV_8UC1);
    CHECK(out.cols == 640 && out.rows == 480);
}

void test_color_frame_converted_to_grayscale() {
    const cv::Mat raw(1080, 1920, CV_8UC3, cv::Scalar(10, 20, 30));
    const cv::Mat out = fsoc::preprocess_live_frame(raw);
    CHECK(out.type() == CV_8UC1);
    CHECK(out.cols == 640 && out.rows == 480);
}

void test_bgra_frame_converted() {
    const cv::Mat raw(720, 1280, CV_8UC4, cv::Scalar(10, 20, 30, 255));
    const cv::Mat out = fsoc::preprocess_live_frame(raw);
    CHECK(out.type() == CV_8UC1);
    CHECK(out.cols == 640 && out.rows == 480);
}

void test_custom_target_size() {
    const cv::Mat raw(480, 640, CV_8UC1, cv::Scalar(50));
    fsoc::LivePreprocessConfig config{};
    config.target_width_px = 320;
    config.target_height_px = 240;
    const cv::Mat out = fsoc::preprocess_live_frame(raw, config);
    CHECK(out.cols == 320 && out.rows == 240);
}

void test_empty_frame_throws() {
    CHECK_THROWS(fsoc::preprocess_live_frame(cv::Mat{}));
}

void test_invalid_target_size_throws() {
    const cv::Mat raw(480, 640, CV_8UC1, cv::Scalar(50));
    fsoc::LivePreprocessConfig config{};
    config.target_width_px = 0;
    CHECK_THROWS(fsoc::preprocess_live_frame(raw, config));
}

void test_unsupported_channel_count_throws() {
    const cv::Mat raw(480, 640, CV_8UC2, cv::Scalar(1, 2));
    CHECK_THROWS(fsoc::preprocess_live_frame(raw));
}

}  // namespace

int main() {
    test_grayscale_passthrough_resized();
    test_color_frame_converted_to_grayscale();
    test_bgra_frame_converted();
    test_custom_target_size();
    test_empty_frame_throws();
    test_invalid_target_size_throws();
    test_unsupported_channel_count_throws();

    if (failures == 0) {
        std::cout << "PASS: 7 live-preprocessing checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
