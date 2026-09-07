// Stage 4 — Tracker ablation (P0-v2, MVP V2 Phase E / F / H).
//
// A NEW, additive evaluation tool, separate from stage4_evaluation's frozen
// protocol run (docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md) -- it never edits
// that report or its numbers. This tool answers one question empirically,
// across the SAME 11 frozen scenarios / 5 frozen seeds / 8s-@-50Hz closed
// loop: does layering fsoc::TargetTracker's temporal-consistency gate
// (fsoc/target_tracker.hpp) AFTER resolve_perception() reduce the Classical
// clutter false-lock rate documented in docs/MVP_METRICS.md ("Classical's
// naive brightest-connected-component rule false-locks onto bright clutter
// far more than expected in isolation"), and at what coverage cost?
//
// Four configurations, all driving the SAME frozen BeaconDetector /
// AiBeaconDetector / resolve_perception() / PID -- nothing here reimplements
// detection, fusion, or control math:
//   A. CLASSICAL           -- Stage-4 baseline, unchanged
//   B. CLASSICAL+TRACKER   -- Classical's own output gated by TargetTracker
//   C. HYBRID              -- Safe Hybrid (ADR-018), unchanged
//   D. HYBRID+TRACKER (V2) -- Safe Hybrid's fused output gated by TargetTracker
//
// Usage: stage4_tracker_ablation [--model PATH] [--out DIR] [--quick]

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/stage4_closed_loop_bench.hpp"
#include "fsoc/stage4_scenarios.hpp"
#include "fsoc/target_tracker.hpp"

namespace fs = std::filesystem;
using fsoc::AiBeaconDetector;
using fsoc::AiBeaconDetectorConfig;
using fsoc::BeaconDetector;
using fsoc::BeaconDetectorConfig;
using fsoc::PerceptionMode;
using fsoc::TargetTrackerConfig;
namespace stage4 = fsoc::stage4;

namespace {

struct Options {
    std::string model_path{"models/tiny_beacon_net.onnx"};
    std::string out_dir{"generated/ai_stage4_ablation"};
    bool quick{false};
};

[[nodiscard]] Options parse_args(const int argc, char** argv) {
    Options opt{};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            opt.model_path = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            opt.out_dir = argv[++i];
        } else if (arg == "--quick") {
            opt.quick = true;
        }
    }
    return opt;
}

struct AblationConfig {
    const char* label;
    PerceptionMode mode;
    bool tracker_enabled;
};

// Default TargetTrackerConfig{} / 0.4 confidence threshold -- the exact same
// defaults fsoc::SimulationRunner and fsoc::baseline_runner_config() use, so
// this evaluates the policy a real run would actually get, not a
// hand-picked-for-this-benchmark configuration.
constexpr std::array<AblationConfig, 4> kConfigs = {{
    {"CLASSICAL", PerceptionMode::Classical, false},
    {"CLASSICAL+TRACKER", PerceptionMode::Classical, true},
    {"HYBRID", PerceptionMode::Hybrid, false},
    {"HYBRID+TRACKER(V2)", PerceptionMode::Hybrid, true},
}};

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);
    const double closed_loop_duration_s = opt.quick ? 2.0 : 8.0;
    if (opt.quick) {
        std::cout << "*** --quick: NOT the frozen protocol counts (smoke test only) ***\n";
    }

    fs::create_directories(opt.out_dir + "/csv");

    AiBeaconDetectorConfig ai_config{};
    ai_config.model_path = opt.model_path;
    ai_config.presence_threshold = 0.95;

    std::unique_ptr<AiBeaconDetector> ai_detector;
    try {
        ai_detector = std::make_unique<AiBeaconDetector>(ai_config);
    } catch (const std::exception& e) {
        std::cerr << "stage4_tracker_ablation: failed to load model '" << opt.model_path << "': " << e.what()
                  << '\n';
        return 1;
    }
    const BeaconDetector classical_detector{BeaconDetectorConfig{}};
    const TargetTrackerConfig tracker_config{};
    constexpr double kTrackerMinConfidenceToSteer = 0.4;

    stage4::ClosedLoopBenchConfig cl_bench{};
    cl_bench.duration_s = closed_loop_duration_s;
    cl_bench.dt_s = 0.02;
    cl_bench.warmup_frames_for_latency = opt.quick ? 2 : 20;

    std::ofstream csv(opt.out_dir + "/csv/tracker_ablation.csv");
    csv << "scenario,config,total_frames,accepted_detection_fraction,target_lost_frames,longest_loss_streak,"
           "rms_angular_error_deg,p95_angular_error_deg,max_angular_error_deg,control_outlier_gt20,"
           "control_outlier_gt50,control_outlier_gt100,max_control_error_px\n";

    constexpr double kRadToDeg = 180.0 / std::numbers::pi_v<double>;
    std::array<std::vector<stage4::ClosedLoopRawSeedResult>, kConfigs.size()> overall_raw_by_config;

    std::cout << "\n=== STAGE-4 TRACKER ABLATION (P0-v2 Phase E/F/H) -- " << closed_loop_duration_s
              << "s @ 50Hz x " << stage4::kStage4BaseSeeds.size() << " seeds ===\n";

    for (const auto scenario : stage4::kAllScenarios) {
        const std::string token(stage4::scenario_token(scenario));
        std::cout << "[" << token << "] " << stage4::scenario_name(scenario) << "\n";
        for (std::size_t c = 0; c < kConfigs.size(); ++c) {
            const AblationConfig& cfg = kConfigs[c];
            std::vector<stage4::ClosedLoopRawSeedResult> per_seed;
            for (std::size_t s = 0; s < stage4::kStage4BaseSeeds.size(); ++s) {
                per_seed.push_back(stage4::run_closed_loop_scenario_mode_seed(
                    scenario, cfg.mode, stage4::kStage4BaseSeeds[s], s, cl_bench, classical_detector, *ai_detector,
                    cfg.tracker_enabled ? std::optional<TargetTrackerConfig>(tracker_config) : std::nullopt,
                    kTrackerMinConfidenceToSteer));
            }
            const auto finalized = stage4::finalize_closed_loop_results(scenario, cfg.mode, per_seed);
            overall_raw_by_config[c].insert(
                overall_raw_by_config[c].end(), per_seed.begin(), per_seed.end());

            csv << token << ',' << cfg.label << ',' << finalized.total_frames << ','
                << finalized.accepted_detection_fraction << ',' << finalized.target_lost_frames << ','
                << finalized.longest_loss_streak << ',' << finalized.rms_angular_error_rad * kRadToDeg << ','
                << finalized.p95_angular_error_rad * kRadToDeg << ',' << finalized.max_angular_error_rad * kRadToDeg
                << ',' << finalized.control_outlier_gt20 << ',' << finalized.control_outlier_gt50 << ','
                << finalized.control_outlier_gt100 << ',' << finalized.max_control_error_px << '\n';

            std::cout << std::fixed << std::setprecision(4) << "  " << std::setw(20) << std::left << cfg.label
                       << std::right << " accepted=" << finalized.accepted_detection_fraction
                       << " ctrl_outlier(>20/50/100)=" << finalized.control_outlier_gt20 << "/"
                       << finalized.control_outlier_gt50 << "/" << finalized.control_outlier_gt100
                       << " max_ctrl_err_px=" << finalized.max_control_error_px
                       << " RMS_deg=" << finalized.rms_angular_error_rad * kRadToDeg << "\n";
        }
    }
    csv.close();

    std::cout << "\n--- OVERALL (all " << stage4::kAllScenarios.size() << " scenarios pooled) ---\n";
    for (std::size_t c = 0; c < kConfigs.size(); ++c) {
        const auto overall =
            stage4::finalize_closed_loop_results(stage4::ScenarioId::Clean, kConfigs[c].mode, overall_raw_by_config[c]);
        std::cout << std::fixed << std::setprecision(4) << std::setw(20) << std::left << kConfigs[c].label
                  << std::right << " accepted=" << overall.accepted_detection_fraction
                  << " lost=" << overall.target_lost_frames
                  << " ctrl_outlier(>20/50/100)=" << overall.control_outlier_gt20 << "/"
                  << overall.control_outlier_gt50 << "/" << overall.control_outlier_gt100
                  << " max_ctrl_err_px=" << overall.max_control_error_px
                  << " RMS_deg=" << overall.rms_angular_error_rad * kRadToDeg << "\n";
    }

    std::cout << "\nWrote " << opt.out_dir << "/csv/tracker_ablation.csv\n";
    return 0;
}
