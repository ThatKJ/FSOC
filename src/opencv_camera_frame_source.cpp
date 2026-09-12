#include "fsoc/opencv_camera_frame_source.hpp"

#include <chrono>
#include <stdexcept>

namespace fsoc {

void OpenCVCameraFrameSourceConfig::validate() const {
    const bool has_index = camera_index.has_value();
    const bool has_url = url.has_value() && !url->empty();
    if (has_index == has_url) {
        throw std::invalid_argument(
            "OpenCVCameraFrameSourceConfig: exactly one of camera_index or url must be set.");
    }
}

OpenCVCameraFrameSource::OpenCVCameraFrameSource(OpenCVCameraFrameSourceConfig config)
    : config_(std::move(config)) {
    config_.validate();
}

namespace {
double monotonic_seconds() {
    using clock = std::chrono::steady_clock;
    static const auto epoch = clock::now();
    return std::chrono::duration<double>(clock::now() - epoch).count();
}
}  // namespace

bool OpenCVCameraFrameSource::open() {
    if (opened_) {
        return true;
    }

    bool ok = false;
    if (config_.camera_index.has_value()) {
        ok = capture_.open(*config_.camera_index, cv::CAP_ANY);
    } else {
        ok = capture_.open(*config_.url, cv::CAP_ANY);
    }
    if (!ok || !capture_.isOpened()) {
        return false;
    }

    if (config_.requested_width_px.has_value()) {
        capture_.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(*config_.requested_width_px));
    }
    if (config_.requested_height_px.has_value()) {
        capture_.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(*config_.requested_height_px));
    }

    // Read the ACTUAL negotiated size / rate back — never trust the request.
    const int actual_width = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
    const int actual_height = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
    const double reported_fps = capture_.get(cv::CAP_PROP_FPS);

    info_ = FrameSourceInfo{
        .kind = FrameSourceKind::OpenCVCamera,
        .width_px = actual_width,
        .height_px = actual_height,
        .fps = (reported_fps > 0.0) ? std::optional<double>(reported_fps) : std::nullopt,
        .backend_name = capture_.getBackendName(),
        .description = config_.camera_index.has_value()
                           ? ("camera index " + std::to_string(*config_.camera_index))
                           : ("network url " + *config_.url),
    };

    next_frame_index_ = 0;
    opened_at_s_ = monotonic_seconds();
    opened_ = true;
    return true;
}

bool OpenCVCameraFrameSource::read(Frame& frame) {
    if (!opened_) {
        return false;
    }
    cv::Mat image;
    if (!capture_.read(image) || image.empty()) {
        return false;
    }
    frame.image = image;
    frame.frame_index = next_frame_index_++;
    frame.timestamp_s = monotonic_seconds() - opened_at_s_;
    return true;
}

void OpenCVCameraFrameSource::close() {
    if (opened_) {
        capture_.release();
        opened_ = false;
    }
}

FrameSourceInfo OpenCVCameraFrameSource::info() const {
    return info_;
}

}  // namespace fsoc
