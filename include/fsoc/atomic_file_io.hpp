#pragma once

#include <filesystem>
#include <string>

namespace fsoc {

// Writes `content` to `final_path` via write-to-temp-then-atomic-rename
// (std::filesystem::rename is atomic on POSIX within the same filesystem), so a
// concurrent reader can never observe a partially-written file at `final_path`.
// Shared by LiveFramePublisher and RealSessionRecorder, both of which need this
// exact guarantee for files a browser/frontend polls while fsoc_live is still
// writing. Throws std::runtime_error if the temp file cannot be written or the
// rename fails.
void write_text_atomic(const std::filesystem::path& final_path, const std::string& content);

}  // namespace fsoc
