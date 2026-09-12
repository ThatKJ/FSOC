// Mobile Phone Camera-in-the-Loop milestone: LiveFramePublisher unit checks.
// Same lightweight harness as tests/frame_source_tests.cpp.
//
// Verifies the identity/atomicity contract documented in
// docs/LIVE_DATA_AUDIT.md section 2 and include/fsoc/live_frame_publisher.hpp:
// every frame index the manifest ever names must have BOTH its image and
// telemetry files present on disk, no stray .tmp files survive a publish, and
// old pairs are pruned once they fall outside the retention window.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <opencv2/core.hpp>

#include "fsoc/live_frame_publisher.hpp"

namespace fs = std::filesystem;

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

fs::path make_scratch_dir() {
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
    fs::path dir = fs::temp_directory_path() / ("fsoc_live_frame_publisher_tests_" + std::to_string(now_ns));
    fs::create_directories(dir);
    return dir;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void test_config_validation() {
    fsoc::LiveFramePublisherConfig empty_dir{};
    empty_dir.output_dir = "";
    CHECK_THROWS(empty_dir.validate());

    fsoc::LiveFramePublisherConfig too_few{};
    too_few.output_dir = "generated/live";
    too_few.retain_pairs = 1;
    CHECK_THROWS(too_few.validate());
}

void test_publish_writes_complete_matched_pair() {
    const fs::path dir = make_scratch_dir();
    fsoc::LiveFramePublisherConfig config{};
    config.output_dir = dir.string();
    config.retain_pairs = 3;
    fsoc::LiveFramePublisher publisher(config);

    const cv::Mat frame(4, 4, CV_8UC1, cv::Scalar(128));
    publisher.publish(0, R"({"frameIndex":0})", frame);

    CHECK(fs::exists(publisher.frame_path(0)));
    CHECK(fs::exists(publisher.telemetry_path(0)));
    CHECK(fs::exists(publisher.manifest_path()));
    CHECK(read_file(publisher.telemetry_path(0)) == R"({"frameIndex":0})");

    // No leftover .tmp files after a successful publish.
    for (const auto& entry : fs::directory_iterator(dir)) {
        CHECK(entry.path().extension() != ".tmp");
    }

    const std::string manifest = read_file(publisher.manifest_path());
    CHECK(manifest.find("\"frameIndex\": 0") != std::string::npos);
    CHECK(manifest.find("\"frameFile\": \"frame_0.jpg\"") != std::string::npos);
    CHECK(manifest.find("\"telemetryFile\": \"telemetry_0.json\"") != std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_manifest_always_names_a_complete_pair_across_many_publishes() {
    const fs::path dir = make_scratch_dir();
    fsoc::LiveFramePublisherConfig config{};
    config.output_dir = dir.string();
    config.retain_pairs = 2;
    fsoc::LiveFramePublisher publisher(config);

    const cv::Mat frame(4, 4, CV_8UC1, cv::Scalar(64));
    constexpr std::size_t kFrames = 25;
    for (std::size_t i = 0; i < kFrames; ++i) {
        std::ostringstream telemetry;
        telemetry << R"({"frameIndex":)" << i << "}";
        publisher.publish(i, telemetry.str(), frame);

        // The invariant this class exists to guarantee: whatever frame index the
        // manifest names right after a publish, that exact pair exists on disk.
        const std::string manifest = read_file(publisher.manifest_path());
        std::ostringstream expect;
        expect << "\"frameIndex\": " << i;
        CHECK(manifest.find(expect.str()) != std::string::npos);
        CHECK(fs::exists(publisher.frame_path(i)));
        CHECK(fs::exists(publisher.telemetry_path(i)));
    }

    // Retention: only the last (retain_pairs) indices should still be on disk; older
    // ones must have been pruned so a long session cannot grow this directory forever.
    const std::size_t last = kFrames - 1;
    for (std::size_t i = 0; i + config.retain_pairs <= last; ++i) {
        CHECK(!fs::exists(publisher.frame_path(i)));
        CHECK(!fs::exists(publisher.telemetry_path(i)));
    }
    CHECK(fs::exists(publisher.frame_path(last)));
    CHECK(fs::exists(publisher.telemetry_path(last)));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace

int main() {
    test_config_validation();
    test_publish_writes_complete_matched_pair();
    test_manifest_always_names_a_complete_pair_across_many_publishes();

    if (failures == 0) {
        std::cout << "PASS: 3 LiveFramePublisher checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
