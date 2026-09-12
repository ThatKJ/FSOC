// G2 real-data recording: RealSessionRecorder unit checks. Same lightweight harness
// as tests/live_frame_publisher_tests.cpp.
//
// Verifies: a recording's manifest/frames/telemetry survive independently of the
// live preview buffer (nothing here is pruned), a write failure is counted and does
// not throw or stop recording, event markers land tagged with the last frame, and
// stop() finalizes the manifest with an end timestamp.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <opencv2/core.hpp>

#include "fsoc/real_session_recorder.hpp"

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
    fs::path dir = fs::temp_directory_path() / ("fsoc_real_session_recorder_tests_" + std::to_string(now_ns));
    fs::create_directories(dir);
    return dir;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fsoc::RealSessionRecorderConfig base_config(const fs::path& dir) {
    fsoc::RealSessionRecorderConfig config{};
    config.output_root = dir.string();
    config.session_id = "test-session-1";
    config.calibration_status = "UNCALIBRATED";
    config.calibration_id = "NONE";
    config.perception_mode = "CLASSICAL";
    config.software_commit = "deadbeef";
    config.cli_args = "--source camera --camera-index 0 --uncalibrated";
    config.source_backend = "AVFoundation";
    config.source_description = "index 0";
    config.raw_width_px = 1280;
    config.raw_height_px = 720;
    config.preprocessed_width_px = 640;
    config.preprocessed_height_px = 480;
    return config;
}

void test_config_validation() {
    fsoc::RealSessionRecorderConfig missing_root = base_config(fs::temp_directory_path());
    missing_root.output_root = "";
    CHECK_THROWS(fsoc::RealSessionRecorder{missing_root});

    fsoc::RealSessionRecorderConfig missing_session = base_config(fs::temp_directory_path());
    missing_session.session_id = "";
    CHECK_THROWS(fsoc::RealSessionRecorder{missing_session});
}

void test_recording_survives_and_is_never_pruned() {
    const fs::path dir = make_scratch_dir();
    fsoc::RealSessionRecorder recorder(base_config(dir));
    const std::string recording_id = recorder.recording_id();
    const fs::path recording_dir = dir / recording_id;

    // Manifest exists immediately at start, before any frame.
    CHECK(fs::exists(recording_dir / "manifest.json"));
    CHECK(read_file(recording_dir / "manifest.json").find("\"recordedFrameCount\": 0") != std::string::npos);

    const cv::Mat frame(4, 4, CV_8UC1, cv::Scalar(50));
    constexpr std::size_t kFrames = 40;
    for (std::size_t i = 0; i < kFrames; ++i) {
        std::ostringstream telemetry;
        telemetry << R"({"frameIndex":)" << i << "}";
        recorder.record_frame(i, static_cast<double>(i) * 0.02, telemetry.str(), frame);
    }

    CHECK(recorder.recorded_frame_count() == kFrames);
    CHECK(recorder.error_count() == 0);

    // Unlike LiveFramePublisher's bounded retention, EVERY frame this class accepted
    // must still be on disk -- this is the entire point of a recording surviving the
    // live preview buffer's cleanup.
    for (std::size_t i = 0; i < kFrames; ++i) {
        CHECK(fs::exists(recording_dir / "frames" / ("frame_" + std::to_string(i) + ".jpg")));
    }

    const std::string telemetry_contents = read_file(recording_dir / "telemetry.jsonl");
    std::size_t line_count = 0;
    for (char c : telemetry_contents) {
        if (c == '\n') ++line_count;
    }
    CHECK(line_count == kFrames);
    CHECK(telemetry_contents.find(R"({"frameIndex":0})") != std::string::npos);
    CHECK(telemetry_contents.find(R"({"frameIndex":39})") != std::string::npos);

    const std::string manifest = read_file(recording_dir / "manifest.json");
    CHECK(manifest.find("\"recordedFrameCount\": 40") != std::string::npos);
    CHECK(manifest.find("\"errorCount\": 0") != std::string::npos);
    CHECK(manifest.find("\"sessionId\": \"test-session-1\"") != std::string::npos);
    CHECK(manifest.find("\"endedAtEpochMs\": null") != std::string::npos);  // stop() not called yet

    // No leftover .tmp files after a successful run.
    for (const auto& entry : fs::recursive_directory_iterator(recording_dir)) {
        CHECK(entry.path().extension() != ".tmp");
    }

    recorder.stop();
    const std::string manifest_after_stop = read_file(recording_dir / "manifest.json");
    CHECK(manifest_after_stop.find("\"endedAtEpochMs\": null") == std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_event_markers_tag_last_frame() {
    const fs::path dir = make_scratch_dir();
    fsoc::RealSessionRecorder recorder(base_config(dir));
    const fs::path recording_dir = dir / recorder.recording_id();

    const cv::Mat frame(4, 4, CV_8UC1, cv::Scalar(10));
    recorder.record_frame(0, 0.0, R"({"frameIndex":0})", frame);
    recorder.record_frame(1, 0.02, R"({"frameIndex":1})", frame);
    recorder.mark_event("beacon_covered");
    recorder.record_frame(2, 0.04, R"({"frameIndex":2})", frame);
    recorder.mark_event("beacon_visible");

    CHECK(recorder.event_count() == 2);
    const std::string events = read_file(recording_dir / "events.jsonl");
    CHECK(events.find(R"("frameIndex": 1)") != std::string::npos);
    CHECK(events.find(R"("label": "beacon_covered")") != std::string::npos);
    CHECK(events.find(R"("frameIndex": 2)") != std::string::npos);
    CHECK(events.find(R"("label": "beacon_visible")") != std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_write_failure_is_counted_not_thrown() {
    const fs::path dir = make_scratch_dir();
    fsoc::RealSessionRecorder recorder(base_config(dir));
    const fs::path recording_dir = dir / recorder.recording_id();

    const cv::Mat frame(4, 4, CV_8UC1, cv::Scalar(10));
    recorder.record_frame(0, 0.0, R"({"frameIndex":0})", frame);
    CHECK(recorder.recorded_frame_count() == 1);

    // Replace the frames/ directory with a file of the same name so the next
    // frame's imwrite (which needs frames/ to be a directory) fails -- a concrete,
    // reproducible disk-error stand-in without requiring a full disk.
    fs::remove_all(recording_dir / "frames");
    std::ofstream blocker(recording_dir / "frames");
    blocker << "not a directory";
    blocker.close();

    // Must not throw despite the write failing underneath.
    recorder.record_frame(1, 0.02, R"({"frameIndex":1})", frame);
    CHECK(recorder.error_count() >= 1);
    CHECK(recorder.recorded_frame_count() == 1);  // unchanged -- the failed frame was not counted as recorded

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace

int main() {
    test_config_validation();
    test_recording_survives_and_is_never_pruned();
    test_event_markers_tag_last_frame();
    test_write_failure_is_counted_not_thrown();

    if (failures == 0) {
        std::cout << "PASS: 4 RealSessionRecorder checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
