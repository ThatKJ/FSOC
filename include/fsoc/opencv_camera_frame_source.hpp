#pragma once

#include <optional>
#include <string>

#include <opencv2/videoio.hpp>

#include "fsoc/frame_source.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// OpenCVCameraFrameSource — physical or network camera via cv::VideoCapture
// ---------------------------------------------------------------------------
//
// Deliberately ONE class for both native device cameras (phone/webcam seen as
// an OS camera index) and network camera streams (MJPEG/RTSP/HTTP URLs a
// local OpenCV build supports): cv::VideoCapture already exposes both through
// the same open()/read() surface, and OpenCVCameraFrameSourceConfig::validate()
// enforces that exactly one of `camera_index` / `url` is set. A second,
// near-identical "NetworkCameraFrameSource" class would duplicate this
// wrapper for no behavioural difference.
//
// Never coupled to BeaconDetector, AiBeaconDetector, or any perception code —
// this header includes only OpenCV's videoio module and fsoc/frame_source.hpp.

struct OpenCVCameraFrameSourceConfig {
    std::optional<int> camera_index{};  // e.g. 0, 1 — OS-enumerated device index
    std::optional<std::string> url{};   // e.g. an MJPEG/RTSP/HTTP stream URL

    // Requested capture size (best-effort — cv::VideoCapture::set() is a hint;
    // the device may ignore it, so callers must read the ACTUAL size back from
    // FrameSourceInfo after open(), never assume these were honored).
    std::optional<int> requested_width_px{};
    std::optional<int> requested_height_px{};

    // Exactly one of camera_index / url must be set. Throws std::invalid_argument
    // otherwise (both set, or neither set).
    void validate() const;
};

class OpenCVCameraFrameSource final : public FrameSource {
public:
    explicit OpenCVCameraFrameSource(OpenCVCameraFrameSourceConfig config);

    [[nodiscard]] bool open() override;
    [[nodiscard]] bool read(Frame& frame) override;
    void close() override;
    [[nodiscard]] FrameSourceInfo info() const override;

private:
    OpenCVCameraFrameSourceConfig config_;
    cv::VideoCapture capture_{};
    FrameSourceInfo info_{};
    std::size_t next_frame_index_{0};
    double opened_at_s_{0.0};
    bool opened_{false};
};

}  // namespace fsoc
