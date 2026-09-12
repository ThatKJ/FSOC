#pragma once

#include <cstddef>
#include <string>

#include <opencv2/core.hpp>

namespace fsoc {

// ---------------------------------------------------------------------------
// LiveFramePublisher — atomic, frame-identity-safe local file transport.
// ---------------------------------------------------------------------------
//
// fsoc_live has no natural "end of run" the way the finite fsoc_demo/CSV path does
// (see docs/PHONE_CAMERA_METRICS.md), so it cannot return one response per request.
// Before this class existed, it overwrote a single frame.jpg and telemetry.json in
// place, polled by two independent, unsynchronized HTTP requests — a reader could
// receive frame N's image paired with frame N+1's telemetry, or a partial write mid-
// overwrite (see docs/LIVE_DATA_AUDIT.md section 2).
//
// LiveFramePublisher instead publishes each frame as an immutable, frame-indexed
// (image, telemetry) pair:
//   1. Write frame_<N>.jpg.tmp and telemetry_<N>.json.tmp.
//   2. Atomically rename both into place (frame_<N>.jpg, telemetry_<N>.json) —
//      std::filesystem::rename is atomic on POSIX within the same filesystem.
//   3. Only THEN atomically flip manifest.json (also via temp+rename) to name frame
//      index N.
// A reader that fetches manifest.json and sees frame index N is therefore guaranteed
// that BOTH frame_<N>.jpg and telemetry_<N>.json already exist, complete, on disk — it
// can never observe a half-written or mismatched pair.
//
// Bounded retention: only the `retain_pairs` most recent (image, telemetry) pairs are
// kept; older ones are deleted after each publish so a long-running session cannot
// grow generated/live/ without limit. `retain_pairs` must be >= 2 so a reader that read
// the manifest just before it advanced can still fetch the previous — still complete —
// pair it named.
//
// This is a pure I/O sink: it never inspects detection, perception, or control
// content. The caller (fsoc_live) is responsible for the telemetry JSON's content;
// this class only guarantees WHEN and HOW it becomes visible on disk.

struct LiveFramePublisherConfig {
    std::string output_dir;

    // Must be >= 2 (see class comment). Defaults to 3.
    std::size_t retain_pairs{3};

    // Throws std::invalid_argument if output_dir is empty or retain_pairs < 2.
    void validate() const;
};

class LiveFramePublisher {
public:
    explicit LiveFramePublisher(LiveFramePublisherConfig config);

    // Publishes one (image, telemetry) pair for `frame_index`, then prunes pairs
    // older than `retain_pairs` behind it. `telemetry_json` is written verbatim — the
    // caller owns its schema/content. Throws std::runtime_error if the image cannot be
    // encoded/written or any rename fails.
    void publish(std::size_t frame_index, const std::string& telemetry_json, const cv::Mat& frame_image);

    // Absolute-or-relative (matches the configured output_dir) path helpers, exposed
    // for tests and for callers that want to log what was just written.
    [[nodiscard]] std::string frame_path(std::size_t frame_index) const;
    [[nodiscard]] std::string telemetry_path(std::size_t frame_index) const;
    [[nodiscard]] std::string manifest_path() const;

private:
    LiveFramePublisherConfig config_;

    void prune_older_than(std::size_t frame_index) const;
};

}  // namespace fsoc
