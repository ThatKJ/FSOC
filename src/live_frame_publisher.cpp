#include "fsoc/live_frame_publisher.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <opencv2/imgcodecs.hpp>

namespace fsoc {

namespace fs = std::filesystem;

void LiveFramePublisherConfig::validate() const {
    if (output_dir.empty()) {
        throw std::invalid_argument("LiveFramePublisherConfig: output_dir must not be empty");
    }
    if (retain_pairs < 2) {
        throw std::invalid_argument(
            "LiveFramePublisherConfig: retain_pairs must be >= 2 (a reader mid-fetch of the "
            "previous pair must still find it)");
    }
}

namespace {

void atomic_rename(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        throw std::runtime_error("LiveFramePublisher: failed to rename " + from.string() + " -> " +
                                  to.string() + ": " + ec.message());
    }
}

void write_text_atomic(const fs::path& final_path, const std::string& content) {
    const fs::path tmp_path = final_path.string() + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::trunc | std::ios::binary);
        if (!out) {
            throw std::runtime_error("LiveFramePublisher: failed to open " + tmp_path.string());
        }
        out << content;
        if (!out) {
            throw std::runtime_error("LiveFramePublisher: failed to write " + tmp_path.string());
        }
    }
    atomic_rename(tmp_path, final_path);
}

}  // namespace

LiveFramePublisher::LiveFramePublisher(LiveFramePublisherConfig config) : config_(std::move(config)) {
    config_.validate();
    std::error_code ec;
    fs::create_directories(config_.output_dir, ec);
    if (ec) {
        throw std::runtime_error("LiveFramePublisher: failed to create output_dir " + config_.output_dir +
                                  ": " + ec.message());
    }
}

std::string LiveFramePublisher::frame_path(std::size_t frame_index) const {
    return (fs::path(config_.output_dir) / ("frame_" + std::to_string(frame_index) + ".jpg")).string();
}

std::string LiveFramePublisher::telemetry_path(std::size_t frame_index) const {
    return (fs::path(config_.output_dir) / ("telemetry_" + std::to_string(frame_index) + ".json")).string();
}

std::string LiveFramePublisher::manifest_path() const {
    return (fs::path(config_.output_dir) / "manifest.json").string();
}

void LiveFramePublisher::publish(std::size_t frame_index, const std::string& telemetry_json,
                                  const cv::Mat& frame_image) {
    const fs::path frame_final = frame_path(frame_index);
    // cv::imwrite picks its codec from the file extension, so the temp name must keep
    // ".jpg" as the actual extension (".tmp" first, not last) rather than the usual
    // "<final>.tmp" convention used for the text files below.
    const fs::path frame_tmp =
        frame_final.parent_path() / (frame_final.stem().string() + ".tmp" + frame_final.extension().string());
    if (!cv::imwrite(frame_tmp.string(), frame_image)) {
        throw std::runtime_error("LiveFramePublisher: cv::imwrite failed for " + frame_tmp.string());
    }
    atomic_rename(frame_tmp, frame_final);

    const fs::path telemetry_final = telemetry_path(frame_index);
    write_text_atomic(telemetry_final, telemetry_json);

    // Manifest is flipped LAST: by this point both files above are already fully
    // renamed into place, so any reader that observes this frame_index in the
    // manifest is guaranteed a complete pair.
    const auto published_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
    std::ostringstream manifest;
    manifest << "{\n"
             << "  \"schemaVersion\": 1,\n"
             << "  \"frameIndex\": " << frame_index << ",\n"
             << "  \"frameFile\": \"" << frame_final.filename().string() << "\",\n"
             << "  \"telemetryFile\": \"" << telemetry_final.filename().string() << "\",\n"
             << "  \"publishedAtEpochMs\": " << published_at_ms << "\n"
             << "}\n";
    write_text_atomic(manifest_path(), manifest.str());

    prune_older_than(frame_index);
}

void LiveFramePublisher::prune_older_than(std::size_t frame_index) const {
    // Only the exact index falling just outside the retention window is removed each
    // call (not a directory scan) — cheap, and correct as long as frame_index is
    // non-decreasing across calls, which LiveFramePublisher's caller guarantees (it is
    // the real camera's own monotonic frame counter).
    if (frame_index < config_.retain_pairs) {
        return;
    }
    const std::size_t stale_index = frame_index - config_.retain_pairs;
    std::error_code ec;
    fs::remove(frame_path(stale_index), ec);
    fs::remove(telemetry_path(stale_index), ec);
}

}  // namespace fsoc
