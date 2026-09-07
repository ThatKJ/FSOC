// AI PERCEPTION V2 — synthetic training-dataset generator.
//
// Builds a deterministic, seeded, domain-randomized dataset of single-channel
// optical frames + training-only labels for TinyBeaconNet. This executable is
// dataset tooling: it is NOT part of the closed-loop control path and touches no
// frozen v1_baseline module.
//
//   generated/ai_dataset/
//   ├── train/  train_000000.png ...        (CV_8UC1, 640x480)
//   ├── val/    val_000000.png ...
//   ├── test/   test_000000.png ...
//   └── metadata/
//       ├── train.jsonl  val.jsonl  test.jsonl   (one label record per line)
//       └── dataset.json                          (generator version, seed,
//                                                  ranges, split membership)
//
// Splits are contiguous, disjoint blocks of one global sample-index space; each
// sample's seed is sample_seed_for(dataset_seed, global_index), so a sample is
// fully addressable and NO frame can leak between splits (see docs/21).
//
// Usage:
//   generate_ai_dataset [--out DIR] [--train N] [--val N] [--test N]
//                       [--neg-frac F] [--seed N] [--width N] [--height N]
//                       [--quiet]

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <opencv2/imgcodecs.hpp>

#include "fsoc/ai_frame_synth.hpp"

namespace fs = std::filesystem;
using fsoc::ai::AiFrameSynthConfig;
using fsoc::ai::AiFrameSynthesizer;
using fsoc::ai::Range;
using fsoc::ai::sample_seed_for;
using fsoc::ai::SynthFrame;

namespace {

struct Options {
    std::string out_dir{"generated/ai_dataset"};
    int train{8000};
    int val{1500};
    int test{1500};
    double neg_frac{0.25};
    std::uint64_t seed{26169};
    int width{640};
    int height{480};
    bool quiet{false};
};

[[noreturn]] void usage_error(const std::string& message) {
    std::cerr << "generate_ai_dataset: " << message << "\n"
              << "usage: generate_ai_dataset [--out DIR] [--train N] [--val N] [--test N]\n"
              << "                           [--neg-frac F] [--seed N] [--width N] [--height N]\n"
              << "                           [--quiet]\n";
    std::exit(2);
}

[[nodiscard]] int parse_int(const std::string& value, const char* flag) {
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        usage_error(std::string(flag) + " expects an integer, got '" + value + "'");
    }
}

[[nodiscard]] double parse_double(const std::string& value, const char* flag) {
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        usage_error(std::string(flag) + " expects a number, got '" + value + "'");
    }
}

[[nodiscard]] Options parse_args(const std::vector<std::string>& args) {
    Options opts{};
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= args.size()) {
                usage_error(std::string(flag) + " requires a value");
            }
            return args[++i];
        };
        if (a == "--out") {
            opts.out_dir = next("--out");
        } else if (a == "--train") {
            opts.train = parse_int(next("--train"), "--train");
        } else if (a == "--val") {
            opts.val = parse_int(next("--val"), "--val");
        } else if (a == "--test") {
            opts.test = parse_int(next("--test"), "--test");
        } else if (a == "--neg-frac") {
            opts.neg_frac = parse_double(next("--neg-frac"), "--neg-frac");
        } else if (a == "--seed") {
            opts.seed = static_cast<std::uint64_t>(std::stoull(next("--seed")));
        } else if (a == "--width") {
            opts.width = parse_int(next("--width"), "--width");
        } else if (a == "--height") {
            opts.height = parse_int(next("--height"), "--height");
        } else if (a == "--quiet") {
            opts.quiet = true;
        } else if (a == "-h" || a == "--help") {
            std::cout << "generate_ai_dataset — synthetic TinyBeaconNet dataset generator\n"
                      << "see docs/20_AI_DATASET_AND_TRAINING.md\n";
            std::exit(0);
        } else {
            usage_error("unknown argument '" + a + "'");
        }
    }
    if (opts.train < 0 || opts.val < 0 || opts.test < 0 || (opts.train + opts.val + opts.test) <= 0) {
        usage_error("split sizes must be >= 0 and sum to > 0");
    }
    if (!(opts.neg_frac >= 0.0 && opts.neg_frac <= 1.0)) {
        usage_error("--neg-frac must be in [0, 1]");
    }
    if (opts.width <= 0 || opts.height <= 0) {
        usage_error("--width / --height must be > 0");
    }
    return opts;
}

// Evenly spaced negatives within a split (Bresenham-style): sample k of n is a
// negative iff the running negative count ticks over between k and k+1. Gives an
// exact negative count of round(n * neg_frac) with no positional clustering.
[[nodiscard]] bool is_negative(const int k, const int n, const int negatives) {
    if (negatives <= 0) {
        return false;
    }
    const long long lo = static_cast<long long>(k) * negatives / n;
    const long long hi = static_cast<long long>(k + 1) * negatives / n;
    return hi != lo;
}

[[nodiscard]] std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

[[nodiscard]] std::string num(const double v) {
    std::ostringstream os;
    os << std::setprecision(10) << v;
    return os.str();
}

[[nodiscard]] std::string range_json(const Range& r) {
    return "[" + num(r.lo) + ", " + num(r.hi) + "]";
}

// One label record (JSONL). x_px/y_px are null for negatives — no sentinel.
[[nodiscard]] std::string record_json(
    const std::string& id,
    const std::string& split,
    const SynthFrame& f,
    const int width,
    const int height,
    const std::uint64_t seed) {
    const auto& p = f.params;
    std::ostringstream os;
    os << "{"
       << "\"id\": \"" << json_escape(id) << "\", "
       << "\"split\": \"" << split << "\", "
       << "\"width\": " << width << ", \"height\": " << height << ", "
       << "\"seed\": " << seed << ", "
       << "\"target_present\": " << (f.target_present ? "true" : "false") << ", "
       << "\"x_px\": " << (f.target_present ? num(f.x_px) : "null") << ", "
       << "\"y_px\": " << (f.target_present ? num(f.y_px) : "null") << ", "
       << "\"beacon_peak\": " << num(p.beacon_peak) << ", "
       << "\"beacon_sigma_px\": " << num(p.beacon_sigma_px) << ", "
       << "\"beacon_anisotropy\": " << num(p.beacon_anisotropy) << ", "
       << "\"beacon_edge_clipped\": " << (p.beacon_edge_clipped ? "true" : "false") << ", "
       << "\"dark_offset\": " << num(p.dark_offset) << ", "
       << "\"gradient_amplitude\": " << num(p.gradient_amplitude) << ", "
       << "\"vignette_strength\": " << num(p.vignette_strength) << ", "
       << "\"optical_mode\": \"" << p.optical_mode << "\", "
       << "\"optical_param\": " << num(p.optical_param) << ", "
       << "\"optical_angle_rad\": " << num(p.optical_angle_rad) << ", "
       << "\"read_sigma\": " << num(p.read_sigma) << ", "
       << "\"shot_scale\": " << num(p.shot_scale) << ", "
       << "\"hot_pixels\": " << p.hot_pixels << ", "
       << "\"dead_pixels\": " << p.dead_pixels << ", "
       << "\"salt_pixels\": " << p.salt_pixels << ", "
       << "\"star_count\": " << p.star_count << ", "
       << "\"has_bright_distractor\": " << (p.has_bright_distractor ? "true" : "false") << ", "
       << "\"brightest_distractor_peak\": " << num(p.brightest_distractor_peak) << ", "
       << "\"has_cluster\": " << (p.has_cluster ? "true" : "false") << ", "
       << "\"difficulty\": " << num(p.difficulty)
       << "}";
    return os.str();
}

[[nodiscard]] std::string config_ranges_json(const AiFrameSynthConfig& c) {
    std::ostringstream os;
    os << "{\n"
       << "    \"negative_fraction\": " << num(c.negative_fraction) << ",\n"
       << "    \"beacon\": {\"peak_intensity\": " << range_json(c.beacon.peak_intensity)
       << ", \"sigma_px\": " << range_json(c.beacon.sigma_px)
       << ", \"anisotropy_ratio\": " << range_json(c.beacon.anisotropy_ratio)
       << ", \"elongation_probability\": " << num(c.beacon.elongation_probability)
       << ", \"max_edge_overshoot_px\": " << num(c.beacon.max_edge_overshoot_px) << "},\n"
       << "    \"background\": {\"dark_offset\": " << range_json(c.background.dark_offset)
       << ", \"gradient_amplitude\": " << range_json(c.background.gradient_amplitude)
       << ", \"vignette_strength\": " << range_json(c.background.vignette_strength) << "},\n"
       << "    \"noise\": {\"read_sigma\": " << range_json(c.noise.read_sigma)
       << ", \"shot_scale\": " << range_json(c.noise.shot_scale)
       << ", \"hot_pixel_count\": " << range_json(c.noise.hot_pixel_count)
       << ", \"dead_pixel_count\": " << range_json(c.noise.dead_pixel_count)
       << ", \"salt_pixel_count\": " << range_json(c.noise.salt_pixel_count) << "},\n"
       << "    \"optical\": {\"weights\": {\"none\": " << num(c.optical.weight_none)
       << ", \"gaussian_blur\": " << num(c.optical.weight_gaussian_blur)
       << ", \"defocus\": " << num(c.optical.weight_defocus)
       << ", \"motion_blur\": " << num(c.optical.weight_motion_blur) << "}"
       << ", \"gaussian_blur_sigma_px\": " << range_json(c.optical.gaussian_blur_sigma_px)
       << ", \"defocus_radius_px\": " << range_json(c.optical.defocus_radius_px)
       << ", \"motion_blur_length_px\": " << range_json(c.optical.motion_blur_length_px) << "},\n"
       << "    \"clutter\": {\"star_count\": " << range_json(c.clutter.star_count)
       << ", \"star_peak_intensity\": " << range_json(c.clutter.star_peak_intensity)
       << ", \"star_sigma_px\": " << range_json(c.clutter.star_sigma_px)
       << ", \"bright_distractor_probability\": " << num(c.clutter.bright_distractor_probability)
       << ", \"bright_distractor_excess\": " << range_json(c.clutter.bright_distractor_excess)
       << ", \"cluster_probability\": " << num(c.clutter.cluster_probability) << "}\n"
       << "  }";
    return os.str();
}

struct SplitStats {
    int count{0};
    int positives{0};
    int negatives{0};
};

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const Options opts = parse_args(args);

    AiFrameSynthConfig config{};
    config.width_px = opts.width;
    config.height_px = opts.height;
    config.negative_fraction = opts.neg_frac;
    try {
        config.validate();
    } catch (const std::exception& e) {
        std::cerr << "generate_ai_dataset: invalid config: " << e.what() << "\n";
        return 1;
    }
    const AiFrameSynthesizer synth(config);

    const fs::path root(opts.out_dir);
    const fs::path meta_dir = root / "metadata";
    std::error_code ec;
    for (const char* split : {"train", "val", "test"}) {
        fs::create_directories(root / split, ec);
    }
    fs::create_directories(meta_dir, ec);

    struct SplitDef {
        std::string name;
        int size;
    };
    const std::vector<SplitDef> splits{
        {"train", opts.train}, {"val", opts.val}, {"test", opts.test}};

    const int total = opts.train + opts.val + opts.test;
    int global_index = 0;
    std::vector<SplitStats> stats(splits.size());

    for (std::size_t s = 0; s < splits.size(); ++s) {
        const SplitDef& sd = splits[s];
        if (sd.size == 0) {
            continue;
        }
        const int negatives = static_cast<int>(std::lround(static_cast<double>(sd.size) *
                                                           opts.neg_frac));
        std::ofstream manifest(meta_dir / (sd.name + ".jsonl"));
        if (!manifest) {
            std::cerr << "generate_ai_dataset: cannot write " << (meta_dir / (sd.name + ".jsonl"))
                      << "\n";
            return 1;
        }

        for (int k = 0; k < sd.size; ++k, ++global_index) {
            const bool negative = is_negative(k, sd.size, negatives);
            const std::uint64_t seed = sample_seed_for(opts.seed, static_cast<std::uint64_t>(
                                                                      global_index));
            const SynthFrame frame = synth.synthesize(seed, !negative);

            std::ostringstream name;
            name << sd.name << "_" << std::setw(6) << std::setfill('0') << k;
            const std::string id = name.str();
            const fs::path png_path = root / sd.name / (id + ".png");

            if (!cv::imwrite(png_path.string(), frame.image)) {
                std::cerr << "generate_ai_dataset: cv::imwrite failed for " << png_path << "\n";
                return 1;
            }
            manifest << record_json(id, sd.name, frame, opts.width, opts.height, seed) << "\n";

            stats[s].count += 1;
            if (frame.target_present) {
                stats[s].positives += 1;
            } else {
                stats[s].negatives += 1;
            }

            if (!opts.quiet && (global_index % 1000 == 0 || global_index == total - 1)) {
                std::cout << "\r  " << (global_index + 1) << " / " << total << " frames"
                          << std::flush;
            }
        }
    }
    if (!opts.quiet) {
        std::cout << "\n";
    }

    // dataset.json — the reproducibility manifest.
    std::ofstream summary(meta_dir / "dataset.json");
    summary << "{\n"
            << "  \"generator_version\": \"" << fsoc::ai::kFrameSynthVersion << "\",\n"
            << "  \"dataset_seed\": " << opts.seed << ",\n"
            << "  \"image_width\": " << opts.width << ",\n"
            << "  \"image_height\": " << opts.height << ",\n"
            << "  \"total_samples\": " << total << ",\n"
            << "  \"index_range_convention\": \"[start, end) half-open\",\n"
            << "  \"global_index_start\": 0,\n"
            << "  \"global_index_end\": " << total << ",\n"
            << "  \"splits\": {\n";
    int cursor = 0;
    for (std::size_t s = 0; s < splits.size(); ++s) {
        const SplitDef& sd = splits[s];
        summary << "    \"" << sd.name << "\": {"
                << "\"count\": " << stats[s].count << ", "
                << "\"positives\": " << stats[s].positives << ", "
                << "\"negatives\": " << stats[s].negatives << ", "
                << "\"global_index_start\": " << cursor << ", "
                << "\"global_index_end\": " << (cursor + sd.size) << "}"
                << (s + 1 < splits.size() ? "," : "") << "\n";
        cursor += sd.size;
    }
    summary << "  },\n"
            << "  \"positive_negative_ratio\": "
            << num(total > 0 ? 1.0 - opts.neg_frac : 0.0) << ",\n"
            << "  \"degradation_ranges\": " << config_ranges_json(config) << "\n"
            << "}\n";

    if (!opts.quiet) {
        std::cout << "wrote " << total << " frames + labels to " << root.string() << "\n";
        for (std::size_t s = 0; s < splits.size(); ++s) {
            std::cout << "  " << splits[s].name << ": " << stats[s].count << " ("
                      << stats[s].positives << " pos / " << stats[s].negatives << " neg)\n";
        }
    }
    return 0;
}
