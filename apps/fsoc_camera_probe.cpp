// fsoc_camera_probe — enumerate plausible camera indices and report what each
// one actually negotiates (resolution / FPS / backend / status). Does NOT
// touch the tracking pipeline; this is pure device discovery (Phase 3).
//
// Run this YOURSELF, interactively, on your own machine — opening a camera
// device may trigger an OS permission prompt (macOS TCC) that only a real
// interactive session can answer.
//
// Usage:
//   fsoc_camera_probe [--max-index N]

#include <iomanip>
#include <iostream>
#include <string>

#include "fsoc/opencv_camera_frame_source.hpp"

namespace {

struct Args {
    int max_index = 4;
};

Args parse_args(int argc, char** argv) {
    Args args{};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--max-index" && i + 1 < argc) {
            args.max_index = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: fsoc_camera_probe [--max-index N]\n"
                      << "Probes camera indices 0..N (default N=4) and reports what each\n"
                      << "negotiates: resolution, FPS, backend, status.\n";
            std::exit(0);
        }
    }
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);

    std::cout << "FSOC camera probe -- indices 0.." << args.max_index << "\n";
    std::cout << std::left << std::setw(6) << "INDEX" << std::setw(14) << "RESOLUTION" << std::setw(10)
              << "FPS" << std::setw(16) << "BACKEND" << "STATUS\n";

    int available_count = 0;
    for (int index = 0; index <= args.max_index; ++index) {
        fsoc::OpenCVCameraFrameSourceConfig config{};
        config.camera_index = index;
        fsoc::OpenCVCameraFrameSource source(config);

        if (!source.open()) {
            std::cout << std::left << std::setw(6) << index << std::setw(14) << "-" << std::setw(10)
                      << "-" << std::setw(16) << "-" << "UNAVAILABLE\n";
            continue;
        }

        const fsoc::FrameSourceInfo info = source.info();
        ++available_count;
        const std::string resolution = std::to_string(info.width_px) + "x" + std::to_string(info.height_px);
        const std::string fps = info.fps.has_value() ? std::to_string(static_cast<int>(*info.fps)) : "?";
        std::cout << std::left << std::setw(6) << index << std::setw(14) << resolution << std::setw(10)
                   << fps << std::setw(16) << info.backend_name << "AVAILABLE\n";
        source.close();
    }

    if (available_count == 0) {
        std::cout << "\nNo camera indices responded. If you expected a phone/webcam here:\n"
                   << "  - macOS: check System Settings > Privacy & Security > Camera and grant\n"
                   << "    this terminal/binary permission, then re-run.\n"
                   << "  - a USB/continuity-camera phone link may need the OS to finish pairing\n"
                   << "    before it appears as a camera index.\n"
                   << "  - try --max-index with a larger value.\n";
        return 1;
    }
    return 0;
}
