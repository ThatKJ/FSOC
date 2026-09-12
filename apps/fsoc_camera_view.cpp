// fsoc_camera_view — minimal live-camera viewer. Proves FSOC can receive
// frames from a real camera reliably, BEFORE any tracking logic runs
// (Phase 4). No GUI dependency: this environment cannot assume a display is
// attached, so frames are saved as periodic JPEG snapshots instead of shown
// in a window; per-frame stats go to stdout.
//
// Run this YOURSELF, interactively — opening a camera device may trigger an
// OS permission prompt only a real interactive session can answer.
//
// Usage:
//   fsoc_camera_view --camera-index 0 [--seconds 60] [--snapshot-every 2.0]
//                     [--out-dir generated/live] [--crosshair]
//   fsoc_camera_view --camera-url "<url>" [--seconds 60] ...

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "fsoc/opencv_camera_frame_source.hpp"

namespace {

struct Args {
    std::optional<int> camera_index{};
    std::optional<std::string> camera_url{};
    double seconds = 60.0;
    double snapshot_every_s = 2.0;
    std::string out_dir = "generated/live";
    bool crosshair = false;
};

void print_usage() {
    std::cout << "Usage:\n"
              << "  fsoc_camera_view --camera-index N [--seconds 60] [--snapshot-every 2.0]\n"
              << "                    [--out-dir generated/live] [--crosshair]\n"
              << "  fsoc_camera_view --camera-url URL  [same options]\n";
}

std::optional<Args> parse_args(int argc, char** argv) {
    Args args{};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--camera-index" && i + 1 < argc) {
            args.camera_index = std::stoi(argv[++i]);
        } else if (arg == "--camera-url" && i + 1 < argc) {
            args.camera_url = std::string(argv[++i]);
        } else if (arg == "--seconds" && i + 1 < argc) {
            args.seconds = std::stod(argv[++i]);
        } else if (arg == "--snapshot-every" && i + 1 < argc) {
            args.snapshot_every_s = std::stod(argv[++i]);
        } else if (arg == "--out-dir" && i + 1 < argc) {
            args.out_dir = argv[++i];
        } else if (arg == "--crosshair") {
            args.crosshair = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else {
            std::cerr << "fsoc_camera_view: unrecognized argument '" << arg << "'\n";
            print_usage();
            return std::nullopt;
        }
    }
    if (args.camera_index.has_value() == args.camera_url.has_value()) {
        std::cerr << "fsoc_camera_view: specify exactly one of --camera-index or --camera-url\n";
        print_usage();
        return std::nullopt;
    }
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed.has_value()) {
        return 2;
    }
    const Args& args = *parsed;

    fsoc::OpenCVCameraFrameSourceConfig config{};
    config.camera_index = args.camera_index;
    config.url = args.camera_url;
    fsoc::OpenCVCameraFrameSource source(config);

    if (!source.open()) {
        std::cerr << "fsoc_camera_view: FAILED to open camera source ("
                  << (args.camera_index.has_value() ? ("index " + std::to_string(*args.camera_index))
                                                     : ("url " + *args.camera_url))
                  << ").\n"
                  << "  This is a clean failure, not a crash. Common causes:\n"
                  << "  - wrong index/URL\n"
                  << "  - camera permission not granted (macOS: System Settings > Privacy & "
                     "Security > Camera)\n"
                  << "  - another process already holds the device\n"
                  << "Run fsoc_camera_probe first to see which indices are AVAILABLE.\n";
        return 1;
    }

    const fsoc::FrameSourceInfo info = source.info();
    std::cout << "Opened " << fsoc::to_string(info.kind) << " (" << info.backend_name
              << "), negotiated " << info.width_px << "x" << info.height_px << "\n";
    std::cout << "Running for " << args.seconds << "s. Snapshots every " << args.snapshot_every_s
              << "s to " << args.out_dir << "/\n";

    std::system(("mkdir -p " + args.out_dir).c_str());

    const auto start = std::chrono::steady_clock::now();
    double last_snapshot_s = -1.0;
    std::size_t frame_count = 0;
    std::size_t consecutive_failures = 0;
    constexpr std::size_t kMaxConsecutiveFailures = 30;
    double last_timestamp_s = 0.0;

    fsoc::Frame frame{};
    while (true) {
        const double elapsed_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (elapsed_s >= args.seconds) {
            break;
        }

        if (!source.read(frame)) {
            ++consecutive_failures;
            std::cerr << "warning: frame read failed (" << consecutive_failures << " consecutive)\n";
            if (consecutive_failures >= kMaxConsecutiveFailures) {
                std::cerr << "fsoc_camera_view: too many consecutive read failures -- camera "
                             "appears disconnected. Exiting cleanly.\n";
                source.close();
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        consecutive_failures = 0;
        ++frame_count;

        if (frame.timestamp_s < last_timestamp_s) {
            std::cerr << "warning: timestamp went backward (" << frame.timestamp_s << " < "
                      << last_timestamp_s << ") -- source clock discontinuity\n";
        }
        last_timestamp_s = frame.timestamp_s;

        if (frame.timestamp_s - last_snapshot_s >= args.snapshot_every_s) {
            cv::Mat out = frame.image.clone();
            if (args.crosshair) {
                const cv::Point center(out.cols / 2, out.rows / 2);
                const cv::Scalar color = out.channels() == 1 ? cv::Scalar(255) : cv::Scalar(0, 255, 0);
                cv::drawMarker(out, center, color, cv::MARKER_CROSS, 40, 2);
            }
            const std::string path =
                args.out_dir + "/frame_" + std::to_string(frame.frame_index) + ".jpg";
            cv::imwrite(path, out);
            last_snapshot_s = frame.timestamp_s;

            const double effective_fps = frame.frame_index > 0 ? static_cast<double>(frame.frame_index) /
                                                                       std::max(frame.timestamp_s, 1e-6)
                                                                 : 0.0;
            std::cout << std::fixed << std::setprecision(2) << "frame " << frame.frame_index << "  t="
                      << frame.timestamp_s << "s  " << frame.image.cols << "x" << frame.image.rows
                      << "  effective_fps=" << effective_fps << "  -> " << path << "\n";
        }
    }

    source.close();
    std::cout << "Done. Captured " << frame_count << " frames over " << args.seconds << "s.\n";
    return 0;
}
