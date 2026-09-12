#include "fsoc/atomic_file_io.hpp"

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace fsoc {

namespace fs = std::filesystem;

void write_text_atomic(const fs::path& final_path, const std::string& content) {
    const fs::path tmp_path = final_path.string() + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::trunc | std::ios::binary);
        if (!out) {
            throw std::runtime_error("write_text_atomic: failed to open " + tmp_path.string());
        }
        out << content;
        if (!out) {
            throw std::runtime_error("write_text_atomic: failed to write " + tmp_path.string());
        }
    }
    std::error_code ec;
    fs::rename(tmp_path, final_path, ec);
    if (ec) {
        throw std::runtime_error("write_text_atomic: failed to rename " + tmp_path.string() + " -> " +
                                  final_path.string() + ": " + ec.message());
    }
}

}  // namespace fsoc
