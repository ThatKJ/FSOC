#include "fsoc/real_session_recorder.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <opencv2/imgcodecs.hpp>

#include "fsoc/atomic_file_io.hpp"

namespace fsoc {

namespace fs = std::filesystem;

void RealSessionRecorderConfig::validate() const {
    if (output_root.empty()) {
        throw std::invalid_argument("RealSessionRecorderConfig: output_root must not be empty");
    }
    if (session_id.empty()) {
        throw std::invalid_argument("RealSessionRecorderConfig: session_id must not be empty");
    }
}

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string opt_size_json(const std::optional<std::size_t>& v) {
    return v.has_value() ? std::to_string(*v) : "null";
}

long long now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

RealSessionRecorder::RealSessionRecorder(RealSessionRecorderConfig config) : config_(std::move(config)) {
    config_.validate();
    recording_id_ = std::to_string(now_epoch_ms());
    started_at_epoch_ms_ = now_epoch_ms();

    std::error_code ec;
    fs::create_directories(fs::path(recording_dir()) / "frames", ec);
    if (ec) {
        throw std::runtime_error("RealSessionRecorder: failed to create " + recording_dir() + "/frames: " +
                                  ec.message());
    }

    write_manifest_best_effort(std::nullopt);
}

std::string RealSessionRecorder::recording_dir() const {
    return (fs::path(config_.output_root) / recording_id_).string();
}

void RealSessionRecorder::record_frame(std::size_t frame_index, double timestamp_s,
                                        const std::string& telemetry_json_line, const cv::Mat& raw_image) {
    last_frame_index_ = frame_index;
    last_timestamp_s_ = timestamp_s;

    try {
        const fs::path frame_final = fs::path(recording_dir()) / "frames" / ("frame_" + std::to_string(frame_index) + ".jpg");
        const fs::path frame_tmp = frame_final.parent_path() /
                                    (frame_final.stem().string() + ".tmp" + frame_final.extension().string());
        if (!cv::imwrite(frame_tmp.string(), raw_image)) {
            throw std::runtime_error("cv::imwrite failed for " + frame_tmp.string());
        }
        std::error_code ec;
        fs::rename(frame_tmp, frame_final, ec);
        if (ec) {
            throw std::runtime_error("rename failed for " + frame_final.string() + ": " + ec.message());
        }

        std::ofstream telemetry_out(fs::path(recording_dir()) / "telemetry.jsonl",
                                     std::ios::app | std::ios::binary);
        if (!telemetry_out) {
            throw std::runtime_error("failed to open telemetry.jsonl for append");
        }
        telemetry_out << telemetry_json_line << "\n";
        if (!telemetry_out) {
            throw std::runtime_error("failed to append to telemetry.jsonl");
        }
    } catch (const std::exception&) {
        // A disk error on one frame must not stall the caller's tracking loop and must
        // not silently disappear either -- count it, keep going.
        ++error_count_;
        return;
    }

    ++recorded_frame_count_;
    write_manifest_best_effort(std::nullopt);
}

void RealSessionRecorder::mark_event(const std::string& label) {
    try {
        std::ostringstream line;
        line << "{\"atEpochMs\": " << now_epoch_ms() << ", \"frameIndex\": " << opt_size_json(last_frame_index_)
             << ", \"timestampS\": " << last_timestamp_s_ << ", \"label\": \"" << json_escape(label) << "\"}";
        std::ofstream events_out(fs::path(recording_dir()) / "events.jsonl", std::ios::app | std::ios::binary);
        if (!events_out) {
            throw std::runtime_error("failed to open events.jsonl for append");
        }
        events_out << line.str() << "\n";
        if (!events_out) {
            throw std::runtime_error("failed to append to events.jsonl");
        }
    } catch (const std::exception&) {
        ++error_count_;
        return;
    }
    ++event_count_;
}

void RealSessionRecorder::stop() { write_manifest_best_effort(now_epoch_ms()); }

void RealSessionRecorder::write_manifest_best_effort(std::optional<long long> ended_at_epoch_ms) const {
    std::ostringstream m;
    m << "{\n"
      << "  \"schemaVersion\": 1,\n"
      << "  \"recordingId\": \"" << json_escape(recording_id_) << "\",\n"
      << "  \"sessionId\": \"" << json_escape(config_.session_id) << "\",\n"
      << "  \"startedAtEpochMs\": " << started_at_epoch_ms_ << ",\n"
      << "  \"endedAtEpochMs\": " << (ended_at_epoch_ms.has_value() ? std::to_string(*ended_at_epoch_ms) : "null")
      << ",\n"
      << "  \"softwareCommit\": \"" << json_escape(config_.software_commit) << "\",\n"
      << "  \"cliArgs\": \"" << json_escape(config_.cli_args) << "\",\n"
      << "  \"calibrationStatus\": \"" << json_escape(config_.calibration_status) << "\",\n"
      << "  \"calibrationId\": \"" << json_escape(config_.calibration_id) << "\",\n"
      << "  \"perceptionMode\": \"" << json_escape(config_.perception_mode) << "\",\n"
      << "  \"aiModelPath\": \"" << json_escape(config_.ai_model_path) << "\",\n"
      << "  \"sourceBackend\": \"" << json_escape(config_.source_backend) << "\",\n"
      << "  \"sourceDescription\": \"" << json_escape(config_.source_description) << "\",\n"
      << "  \"rawWidthPx\": " << config_.raw_width_px << ",\n"
      << "  \"rawHeightPx\": " << config_.raw_height_px << ",\n"
      << "  \"preprocessedWidthPx\": " << config_.preprocessed_width_px << ",\n"
      << "  \"preprocessedHeightPx\": " << config_.preprocessed_height_px << ",\n"
      << "  \"recordedFrameCount\": " << recorded_frame_count_ << ",\n"
      << "  \"errorCount\": " << error_count_ << ",\n"
      << "  \"eventCount\": " << event_count_ << ",\n"
      << "  \"framesDir\": \"frames\",\n"
      << "  \"telemetryFile\": \"telemetry.jsonl\",\n"
      << "  \"eventsFile\": \"events.jsonl\"\n"
      << "}\n";
    try {
        write_text_atomic(fs::path(recording_dir()) / "manifest.json", m.str());
    } catch (const std::exception&) {
        // Best-effort: record_frame()/stop() must not throw. A manifest write failure
        // leaves the PREVIOUS (still valid, still atomic) manifest in place.
    }
}

}  // namespace fsoc
