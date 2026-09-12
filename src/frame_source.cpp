#include "fsoc/frame_source.hpp"

namespace fsoc {

const char* to_string(FrameSourceKind kind) noexcept {
    switch (kind) {
        case FrameSourceKind::Synthetic:
            return "SYNTHETIC";
        case FrameSourceKind::OpenCVCamera:
            return "REAL_CAMERA";
    }
    return "UNKNOWN";
}

}  // namespace fsoc
