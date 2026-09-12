#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

namespace fsoc {

// ---------------------------------------------------------------------------
// FrameSource  (Mobile Phone Camera-in-the-Loop milestone)
// ---------------------------------------------------------------------------
//
// The one abstraction that lets the SAME perception pipeline (BeaconDetector /
// AiBeaconDetector / resolve_perception / compute_tracking_error) consume
// frames from a synthetic renderer OR a real camera device OR a network
// camera stream. FrameSource is a pure I/O boundary: it knows nothing about
// detection, perception mode, tracking, or control, and it never touches
// world truth (TargetState, CameraObservation).
//
// `Frame::image` is exactly what the source produced (grayscale or color,
// whatever resolution the device reports) — resizing/grayscale conversion for
// the detector's CV_8UC1 640x480 contract is a separate, explicit
// preprocessing step (see fsoc/live_preprocessing.hpp), never done silently
// inside a FrameSource implementation.

enum class FrameSourceKind {
    Synthetic,      // deterministic renderer output (SimulationRunner-style)
    OpenCVCamera,   // cv::VideoCapture, by device index OR by URL
};

[[nodiscard]] const char* to_string(FrameSourceKind kind) noexcept;

struct FrameSourceInfo {
    FrameSourceKind kind{FrameSourceKind::Synthetic};
    int width_px{0};
    int height_px{0};
    std::optional<double> fps{};  // nullopt when the source cannot report a rate
    std::string backend_name{};   // e.g. "AVFoundation", "FFMPEG", "Synthetic"
    std::string description{};    // human-readable provenance (index or URL), no secrets
};

// One captured frame plus its provenance. `frame_index` and `timestamp_s` are
// assigned by the FrameSource itself (monotonic, source-clock) — never
// re-derived by a caller.
struct Frame {
    cv::Mat image{};
    std::size_t frame_index{0};
    double timestamp_s{0.0};
};

class FrameSource {
public:
    virtual ~FrameSource() = default;

    // Acquire the device / stream. Returns false (does not throw) on failure
    // so callers can print an actionable diagnostic and exit cleanly instead
    // of crashing — see Phase 20 (failure handling).
    [[nodiscard]] virtual bool open() = 0;

    // Blocks for at most one frame period. Returns false on end-of-stream,
    // a dropped/empty frame, or a closed source; `frame` is left unmodified
    // on failure. Never throws for an ordinary transient read failure (a
    // disconnected camera, a stalled network stream) — that is a false
    // return, not an exception; callers decide whether to retry or fail.
    [[nodiscard]] virtual bool read(Frame& frame) = 0;

    virtual void close() = 0;

    [[nodiscard]] virtual FrameSourceInfo info() const = 0;
};

}  // namespace fsoc
