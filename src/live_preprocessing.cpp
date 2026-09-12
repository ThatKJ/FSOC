#include "fsoc/live_preprocessing.hpp"

#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace fsoc {

cv::Mat preprocess_live_frame(const cv::Mat& raw, const LivePreprocessConfig& config) {
    if (raw.empty()) {
        throw std::invalid_argument("preprocess_live_frame: input frame is empty.");
    }
    if (config.target_width_px <= 0 || config.target_height_px <= 0) {
        throw std::invalid_argument("preprocess_live_frame: target dimensions must be > 0.");
    }

    cv::Mat gray;
    if (raw.channels() == 1) {
        gray = raw;
    } else if (raw.channels() == 3) {
        cv::cvtColor(raw, gray, cv::COLOR_BGR2GRAY);
    } else if (raw.channels() == 4) {
        cv::cvtColor(raw, gray, cv::COLOR_BGRA2GRAY);
    } else {
        throw std::invalid_argument("preprocess_live_frame: unsupported channel count.");
    }

    cv::Mat resized;
    cv::resize(
        gray, resized, cv::Size(config.target_width_px, config.target_height_px), 0, 0, cv::INTER_AREA);
    return resized;
}

}  // namespace fsoc
