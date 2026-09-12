#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

namespace fsoc {

// ---------------------------------------------------------------------------
// RealSessionRecorder — G2 real-data recording sink.
// ---------------------------------------------------------------------------
//
// Writes a self-contained, independently reviewable recording of a live-camera
// session for later annotation and training (docs/LIVE_REALDATA_TASK_STATE.md G2).
// Deliberately separate from LiveFramePublisher: the publisher's files are a
// bounded, PRUNED preview buffer (retain_pairs, oldest pairs deleted every publish)
// — a recording must survive that cleanup, so every accepted frame is written to
// its own directory and never pruned by this class.
//
// Writes are synchronous, per frame, in the same call as the live publish — no
// separate thread, no in-memory queue, so there is no unbounded buffer to overflow.
// A slow/failing disk shows up as a counted write error on that one frame (see
// error_count()), never as a stall of the tracking loop or an accumulating queue.
//
// Layout, under output_root/<recording_id>/:
//   manifest.json          -- identity + running counters, rewritten atomically
//                              after every frame (a crash mid-recording still
//                              leaves a valid, self-describing partial recording)
//   frames/frame_<N>.jpg   -- RAW camera frame: no overlay, no reticle, no
//                              burned-in text (drawn overlays live in a separate,
//                              explicitly-labeled annotated copy if ever offered —
//                              never in this raw training-source directory)
//   telemetry.jsonl        -- one JSON object per line, appended (never rewritten),
//                              same fields as the live per-frame telemetry
//   events.jsonl           -- one JSON object per line, human-marked events

struct RealSessionRecorderConfig {
    std::string output_root{"generated/real_sessions"};
    std::string session_id{};          // the parent fsoc_live session this belongs to
    std::string calibration_status{};  // "CALIBRATED" | "UNCALIBRATED"
    std::string calibration_id{};       // calibration file path, or "NONE"
    std::string perception_mode{};      // "CLASSICAL" | "AI" | "HYBRID"
    std::string ai_model_path{};        // empty when Classical
    std::string software_commit{};      // git commit of the running build
    std::string cli_args{};             // the exact command line fsoc_live was invoked with
    std::string source_backend{};
    std::string source_description{};
    int raw_width_px{};
    int raw_height_px{};
    int preprocessed_width_px{};
    int preprocessed_height_px{};

    // output_root and session_id must not be empty. Throws std::invalid_argument.
    void validate() const;
};

class RealSessionRecorder {
public:
    // Generates a fresh recording_id (wall-clock-ms token, same convention as
    // fsoc_live's sessionId), creates output_root/<recording_id>/{,frames/}, and
    // writes the initial manifest. Throws std::invalid_argument (bad config) or
    // std::runtime_error (filesystem failure) — a recording that failed to start
    // must not silently appear to exist.
    explicit RealSessionRecorder(RealSessionRecorderConfig config);

    // Appends one frame: writes frames/frame_<N>.jpg, one telemetry.jsonl line, and
    // rewrites manifest.json with updated counters. Never throws — a write failure
    // increments error_count() and is otherwise swallowed (the caller's tracking
    // loop must not stall on a disk error); the frame is simply missing from this
    // recording, and the manifest's counters make that honestly visible afterward.
    // `telemetry_json_line` is caller-owned content (one line, no embedded
    // newline) — this class only guarantees where/when it lands, same division of
    // responsibility as LiveFramePublisher.
    void record_frame(std::size_t frame_index, double timestamp_s, const std::string& telemetry_json_line,
                       const cv::Mat& raw_image);

    // Appends one human event marker (e.g. "beacon_covered", "beacon_visible",
    // "scene_change"), tagged with the most recently record_frame()-ed frame index/
    // timestamp. A marker before any frame is recorded is tagged with frameIndex
    // null. Never throws (same reasoning as record_frame(); failures count toward
    // error_count()).
    void mark_event(const std::string& label);

    // Finalizes the manifest (records an end timestamp). Safe to call more than
    // once. A recording that is never stop()-ed (e.g. the process is killed) still
    // has a valid manifest from the last record_frame()'s rewrite, just without an
    // end timestamp — callers/reviewers can tell an unclean stop from a clean one.
    void stop();

    [[nodiscard]] const std::string& recording_id() const noexcept { return recording_id_; }
    [[nodiscard]] std::size_t recorded_frame_count() const noexcept { return recorded_frame_count_; }
    [[nodiscard]] std::size_t error_count() const noexcept { return error_count_; }
    [[nodiscard]] std::size_t event_count() const noexcept { return event_count_; }

private:
    [[nodiscard]] std::string recording_dir() const;
    void write_manifest_best_effort(std::optional<long long> ended_at_epoch_ms) const;

    RealSessionRecorderConfig config_;
    std::string recording_id_;
    long long started_at_epoch_ms_{};
    std::size_t recorded_frame_count_{};
    std::size_t error_count_{};
    std::size_t event_count_{};
    std::optional<std::size_t> last_frame_index_{};
    double last_timestamp_s_{};
};

}  // namespace fsoc
