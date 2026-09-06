// Stage 4 — Classical vs AI vs Safe Hybrid evaluation.
//
// Runs BOTH benchmarks frozen in docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md:
//   A. common-frame perception benchmark (11 scenarios x 5 seeds x 200 frames)
//   B. closed-loop tracking benchmark    (11 scenarios x 3 modes x 5 seeds x 400 frames)
// against the frozen Stage-3 artifacts (ONNX model, presence_threshold=0.95,
// agreement_radius_px=8.0, PID kp=12/ki=0/kd=0). This is evaluation ONLY: it
// implements no detector/PID/hybrid math of its own -- it drives the existing
// fsoc::BeaconDetector / fsoc::AiBeaconDetector / fsoc::resolve_perception and
// the Stage-4 benchmark library (fsoc::stage4).
//
// Usage:
//   stage4_evaluation [--model PATH] [--out DIR] [--quick]
//
// --quick runs a small, clearly-labeled smoke configuration (NOT the frozen
// protocol counts) for fast local iteration; it must never be confused with
// the official Stage-4 numbers, which always come from the default counts.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/ai_frame_synth.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/stage4_closed_loop_bench.hpp"
#include "fsoc/stage4_common_frame_bench.hpp"
#include "fsoc/stage4_metrics.hpp"
#include "fsoc/stage4_scenarios.hpp"

namespace fs = std::filesystem;
using fsoc::AiBeaconDetector;
using fsoc::AiBeaconDetectorConfig;
using fsoc::BeaconDetector;
using fsoc::BeaconDetectorConfig;
using fsoc::PerceptionMode;
namespace stage4 = fsoc::stage4;

namespace {

struct Options {
    std::string model_path{"models/tiny_beacon_net.onnx"};
    std::string out_dir{"generated/ai_stage4"};
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

[[nodiscard]] const char* mode_name(const PerceptionMode mode) {
    switch (mode) {
        case PerceptionMode::Classical: return "CLASSICAL";
        case PerceptionMode::AI: return "AI";
        case PerceptionMode::Hybrid: return "HYBRID";
    }
    return "?";
}

// --------------------------------------------------------------------- //
// Minimal JSON writer (no external dependency — see Stage-3 precedent for
// why: CLAUDE.md "no unnecessary dependencies"; this project's own choice in
// tests/ai_beacon_detector_tests.cpp for the reverse direction (reading)).
// --------------------------------------------------------------------- //
class JsonWriter {
public:
    explicit JsonWriter(std::ostream& out) : out_(out) {}

    void begin_object() { write_prefix(); out_ << "{"; depth_.push_back(false); comma_needed_ = false; }
    void end_object() { depth_.pop_back(); out_ << "}"; comma_needed_ = true; }
    void begin_array() { write_prefix(); out_ << "["; depth_.push_back(false); comma_needed_ = false; }
    void end_array() { depth_.pop_back(); out_ << "]"; comma_needed_ = true; }

    void key(const std::string& k) {
        write_comma();
        out_ << '"' << escape(k) << "\":";
        comma_needed_ = false;
    }
    void value(const double v) { write_prefix(); out_ << format_double(v); comma_needed_ = true; }
    void value(const std::size_t v) { write_prefix(); out_ << v; comma_needed_ = true; }
    void value(const int v) { write_prefix(); out_ << v; comma_needed_ = true; }
    void value(const bool v) { write_prefix(); out_ << (v ? "true" : "false"); comma_needed_ = true; }
    void value(const std::string& v) { write_prefix(); out_ << '"' << escape(v) << '"'; comma_needed_ = true; }
    void value(const char* v) { value(std::string(v)); }

private:
    void write_prefix() { write_comma(); }
    void write_comma() {
        if (comma_needed_) {
            out_ << ",";
        }
    }
    [[nodiscard]] static std::string escape(const std::string& s) {
        std::string out;
        for (const char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }
    [[nodiscard]] static std::string format_double(const double v) {
        std::ostringstream ss;
        ss << std::setprecision(10) << v;
        return ss.str();
    }

    std::ostream& out_;
    std::vector<bool> depth_;
    bool comma_needed_{false};
};

void write_perception_rate_json(JsonWriter& j, const stage4::PerceptionRateStats& s) {
    j.begin_object();
    j.key("total_frames"); j.value(s.total_frames);
    j.key("positive_frames"); j.value(s.positive_frames);
    j.key("negative_frames"); j.value(s.negative_frames);
    j.key("accepted_frames"); j.value(s.accepted_frames);
    j.key("true_positive"); j.value(s.true_positive);
    j.key("false_positive"); j.value(s.false_positive);
    j.key("accepted_rate"); j.value(s.accepted_rate);
    j.key("recall"); j.value(s.recall);
    j.key("precision"); j.value(s.precision);
    j.key("fpr"); j.value(s.fpr);
    j.key("localization");
    j.begin_object();
    j.key("n"); j.value(s.localization.n);
    j.key("median_px"); j.value(s.localization.median_px);
    j.key("mae_px"); j.value(s.localization.mae_px);
    j.key("rmse_px"); j.value(s.localization.rmse_px);
    j.key("p95_px"); j.value(s.localization.p95_px);
    j.key("max_px"); j.value(s.localization.max_px);
    j.end_object();
    j.key("outliers");
    j.begin_object();
    j.key("gt_20px"); j.value(s.outliers.gt_20);
    j.key("gt_50px"); j.value(s.outliers.gt_50);
    j.key("gt_100px"); j.value(s.outliers.gt_100);
    j.end_object();
    j.end_object();
}

void write_hybrid_sources_json(JsonWriter& j, const stage4::HybridSourceCounts& s) {
    j.begin_object();
    j.key("hybrid_agreement"); j.value(s.hybrid_agreement);
    j.key("classical"); j.value(s.classical);
    j.key("ai_only_unverified"); j.value(s.ai_only_unverified);
    j.key("detector_disagreement"); j.value(s.detector_disagreement);
    j.key("none_not_applicable"); j.value(s.none_not_applicable);
    j.end_object();
}

void write_closed_loop_json(JsonWriter& j, const stage4::ClosedLoopScenarioModeResult& r) {
    j.begin_object();
    j.key("total_frames"); j.value(r.total_frames);
    j.key("accepted_detection_fraction"); j.value(r.accepted_detection_fraction);
    j.key("target_lost_frames"); j.value(r.target_lost_frames);
    j.key("longest_loss_streak"); j.value(r.longest_loss_streak);
    j.key("reacquisition_count"); j.value(r.reacquisition_count);
    j.key("mean_reacquisition_time_frames"); j.value(r.mean_reacquisition_time_frames);
    j.key("rms_angular_error_deg"); j.value(r.rms_angular_error_rad * 180.0 / std::numbers::pi_v<double>);
    j.key("median_angular_error_deg"); j.value(r.median_angular_error_rad * 180.0 / std::numbers::pi_v<double>);
    j.key("p95_angular_error_deg"); j.value(r.p95_angular_error_rad * 180.0 / std::numbers::pi_v<double>);
    j.key("max_angular_error_deg"); j.value(r.max_angular_error_rad * 180.0 / std::numbers::pi_v<double>);
    j.key("saturated_actuator_frames"); j.value(r.saturated_actuator_frames);
    j.key("max_commanded_pan_rate_deg_s"); j.value(r.max_commanded_pan_rate_rad_s * 180.0 / std::numbers::pi_v<double>);
    j.key("max_commanded_tilt_rate_deg_s"); j.value(r.max_commanded_tilt_rate_rad_s * 180.0 / std::numbers::pi_v<double>);
    j.key("control_outlier_gt_20px"); j.value(r.control_outlier_gt20);
    j.key("control_outlier_gt_50px"); j.value(r.control_outlier_gt50);
    j.key("control_outlier_gt_100px"); j.value(r.control_outlier_gt100);
    j.key("max_control_error_px"); j.value(r.max_control_error_px);
    j.key("perception_latency_mean_ms"); j.value(r.perception_latency_mean_ms);
    j.key("perception_latency_p95_ms"); j.value(r.perception_latency_p95_ms);
    if (r.mode == PerceptionMode::Hybrid) {
        j.key("hybrid_sources");
        write_hybrid_sources_json(j, r.hybrid_sources);
    }
    j.end_object();
}

}  // namespace

// ---------------------------------------------------------------------------
// Curated evidence (protocol §11): walks scenarios/seeds/frames in
// deterministic table order and saves the FIRST frame matching each of the 8
// required cases -- never cherry-picked after browsing candidates. Uses the
// exact same production resolve_perception() path as the benchmark itself.
// ---------------------------------------------------------------------------
namespace {

void capture_curated_evidence(
    const std::string& evidence_dir, const BeaconDetector& classical_detector, const AiBeaconDetector& ai_detector) {
    std::array<std::string, 8> slot_names = {
        "1_classical_correct_ai_agrees", "2_classical_only",       "3_ai_only_rejected",
        "4_disagreement_rejected",       "5_prevented_wrong_lock", "6_target_absent_rejected",
        "7_hybrid_sacrifices_coverage",  "8_hybrid_no_benefit",
    };
    std::array<bool, 8> found{};
    found.fill(false);

    std::ofstream manifest(evidence_dir + "/manifest.json");
    manifest << "[\n";
    bool first_entry = true;

    const auto save_case = [&](const int slot, const stage4::ScenarioId scenario,
                                const fsoc::ai::SynthFrame& sample, const std::optional<fsoc::BeaconDetection>& classical,
                                const std::optional<fsoc::AiBeaconDetection>& ai, const fsoc::PerceptionResult& hybrid,
                                const std::uint64_t seed, const std::size_t frame_index, const std::string& note) {
        if (found[static_cast<std::size_t>(slot)]) {
            return;
        }
        found[static_cast<std::size_t>(slot)] = true;

        cv::Mat bgr;
        cv::cvtColor(sample.image, bgr, cv::COLOR_GRAY2BGR);
        if (sample.target_present) {
            cv::circle(bgr, cv::Point(static_cast<int>(sample.x_px), static_cast<int>(sample.y_px)), 10,
                       cv::Scalar(255, 255, 0), 1);  // cyan: truth
        }
        if (classical.has_value()) {
            cv::circle(
                bgr, cv::Point(static_cast<int>(classical->centroid_px.x_px), static_cast<int>(classical->centroid_px.y_px)),
                6, cv::Scalar(0, 255, 0), 2);  // green: classical
        }
        if (ai.has_value()) {
            cv::circle(
                bgr,
                cv::Point(static_cast<int>(ai->detection.centroid_px.x_px), static_cast<int>(ai->detection.centroid_px.y_px)),
                8, cv::Scalar(0, 165, 255), 2);  // orange: AI candidate
        }
        if (hybrid.detection.has_value()) {
            cv::circle(
                bgr,
                cv::Point(static_cast<int>(hybrid.detection->centroid_px.x_px), static_cast<int>(hybrid.detection->centroid_px.y_px)),
                14, cv::Scalar(255, 255, 255), 1);  // white ring: accepted control-facing output
        }
        cv::putText(bgr, slot_names[static_cast<std::size_t>(slot)], cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(255, 255, 255), 1);
        cv::imwrite(evidence_dir + "/" + slot_names[static_cast<std::size_t>(slot)] + ".png", bgr);

        if (!first_entry) {
            manifest << ",\n";
        }
        first_entry = false;
        manifest << "  {\"case\": \"" << slot_names[static_cast<std::size_t>(slot)] << "\", \"scenario\": \""
                 << stage4::scenario_name(scenario) << "\", \"seed\": " << seed << ", \"frame_index\": " << frame_index
                 << ", \"truth_present\": " << (sample.target_present ? "true" : "false") << ", \"note\": \"" << note
                 << "\"}";
        std::cout << "  evidence[" << (slot + 1) << "] " << slot_names[static_cast<std::size_t>(slot)]
                  << " <- scenario=" << stage4::scenario_name(scenario) << " seed=" << seed
                  << " frame=" << frame_index << "\n";
    };

    const auto all_found = [&found]() { return std::all_of(found.begin(), found.end(), [](const bool b) { return b; }); };

    for (const auto scenario : stage4::kAllScenarios) {
        if (all_found()) {
            break;
        }
        const auto config = stage4::common_frame_config(scenario);
        const fsoc::ai::AiFrameSynthesizer synth{config};
        const int idx = stage4::scenario_index(scenario);
        const bool force_absent = (scenario == stage4::ScenarioId::TargetAbsent);

        for (const auto base_seed : stage4::kStage4BaseSeeds) {
            if (all_found()) {
                break;
            }
            for (std::size_t i = 0; i < 200; ++i) {
                if (all_found()) {
                    break;
                }
                const auto seed = stage4::frame_seed(idx, base_seed, i);
                const fsoc::ai::SynthFrame sample =
                    force_absent ? synth.synthesize(seed, /*force_target=*/false) : synth.synthesize(seed);
                const auto classical = classical_detector.detect(sample.image);
                const auto ai = ai_detector.detect(sample.image);
                const auto hybrid = fsoc::resolve_perception(PerceptionMode::Hybrid, classical, ai);

                using fsoc::PerceptionRejectionReason;
                using fsoc::PerceptionSource;

                if (hybrid.diagnostics.perception_source == PerceptionSource::HybridAgreement) {
                    save_case(0, scenario, sample, classical, ai, hybrid, base_seed, i,
                              "classical+AI agree within 8px; classical centroid used");
                    if (scenario == stage4::ScenarioId::Clean) {
                        save_case(7, scenario, sample, classical, ai, hybrid, base_seed, i,
                                  "CLEAN scenario: Hybrid output == classical alone here; AI adds confirmation but no "
                                  "measurable localization benefit");
                    }
                }
                if (hybrid.diagnostics.perception_source == PerceptionSource::Classical) {
                    save_case(1, scenario, sample, classical, ai, hybrid, base_seed, i,
                              "classical detected, no AI candidate above threshold");
                }
                if (hybrid.diagnostics.rejection_reason == PerceptionRejectionReason::AiOnlyUnverified) {
                    save_case(2, scenario, sample, classical, ai, hybrid, base_seed, i,
                              "AI candidate present, classical absent -> no control authority (ADR-018)");
                }
                if (hybrid.diagnostics.rejection_reason == PerceptionRejectionReason::DetectorDisagreement) {
                    save_case(3, scenario, sample, classical, ai, hybrid, base_seed, i,
                              "classical/AI centroids > 8px apart -> unconditionally rejected");
                    save_case(6, scenario, sample, classical, ai, hybrid, base_seed, i,
                              "classical alone would have accepted here; Hybrid sacrifices this frame for safety");
                    if (sample.target_present && ai.has_value()) {
                        const double ai_err = std::hypot(
                            ai->detection.centroid_px.x_px - sample.x_px, ai->detection.centroid_px.y_px - sample.y_px);
                        if (ai_err > 50.0) {
                            save_case(
                                4, scenario, sample, classical, ai, hybrid, base_seed, i,
                                "AI candidate error vs truth = " + std::to_string(ai_err) +
                                    "px -- Hybrid rejected this instead of trusting AI's wrong-blob lock");
                        }
                    }
                }
                if (!sample.target_present && !hybrid.detection.has_value()) {
                    save_case(5, scenario, sample, classical, ai, hybrid, base_seed, i,
                              "no true beacon in frame; no accepted control-facing detection");
                }
            }
        }
    }

    manifest << "\n]\n";
    manifest.close();

    for (std::size_t i = 0; i < found.size(); ++i) {
        if (!found[i]) {
            std::cout << "  evidence[" << (i + 1) << "] " << slot_names[i] << " <- NOT FOUND in this run\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);

    const std::size_t frames_per_seed = opt.quick ? 20 : 200;
    const double closed_loop_duration_s = opt.quick ? 2.0 : 8.0;
    if (opt.quick) {
        std::cout << "*** --quick: NOT the frozen protocol counts (smoke test only) ***\n";
    }

    fs::create_directories(opt.out_dir + "/reports");
    fs::create_directories(opt.out_dir + "/csv");
    fs::create_directories(opt.out_dir + "/evidence");

    AiBeaconDetectorConfig ai_config{};
    ai_config.model_path = opt.model_path;
    ai_config.presence_threshold = 0.95;

    std::unique_ptr<AiBeaconDetector> ai_detector;
    try {
        ai_detector = std::make_unique<AiBeaconDetector>(ai_config);
    } catch (const std::exception& e) {
        std::cerr << "stage4_evaluation: failed to load model '" << opt.model_path << "': " << e.what() << '\n';
        return 1;
    }
    const BeaconDetector classical_detector{BeaconDetectorConfig{}};

    // ---------------- A. Common-frame benchmark ----------------
    std::cout << "\n=== A. COMMON-FRAME BENCHMARK (" << frames_per_seed << " frames/seed x "
              << stage4::kStage4BaseSeeds.size() << " seeds) ===\n";

    stage4::PerceptionRateAccumulator overall_classical;
    stage4::PerceptionRateAccumulator overall_ai;
    stage4::PerceptionRateAccumulator overall_hybrid;
    std::vector<stage4::CommonFrameScenarioResult> common_frame_results;

    std::ofstream common_csv(opt.out_dir + "/csv/common_frame.csv");
    common_csv << "scenario,mode,total_frames,positive_frames,negative_frames,accepted_frames,"
                  "true_positive,false_positive,accepted_rate,recall,precision,fpr,"
                  "median_px,mae_px,rmse_px,p95_px,max_px,gt20,gt50,gt100\n";

    const auto write_common_row = [&common_csv](
                                       const std::string& scenario, const std::string& mode,
                                       const stage4::PerceptionRateStats& s) {
        common_csv << scenario << ',' << mode << ',' << s.total_frames << ',' << s.positive_frames << ','
                   << s.negative_frames << ',' << s.accepted_frames << ',' << s.true_positive << ','
                   << s.false_positive << ',' << s.accepted_rate << ',' << s.recall << ',' << s.precision << ','
                   << s.fpr << ',' << s.localization.median_px << ',' << s.localization.mae_px << ','
                   << s.localization.rmse_px << ',' << s.localization.p95_px << ',' << s.localization.max_px
                   << ',' << s.outliers.gt_20 << ',' << s.outliers.gt_50 << ',' << s.outliers.gt_100 << '\n';
    };

    for (const auto scenario : stage4::kAllScenarios) {
        const auto result = stage4::run_common_frame_scenario(
            scenario, stage4::kStage4BaseSeeds, frames_per_seed, classical_detector, *ai_detector,
            &overall_classical, &overall_ai, &overall_hybrid);
        common_frame_results.push_back(result);

        const std::string token(stage4::scenario_token(scenario));
        write_common_row(token, "CLASSICAL", result.classical);
        write_common_row(token, "AI", result.ai);
        write_common_row(token, "HYBRID", result.hybrid);

        std::cout << std::fixed << std::setprecision(3) << "[" << token << "] "
                  << stage4::scenario_name(scenario) << "  n=" << result.total_frames << "\n"
                  << "  CLASSICAL recall=" << result.classical.recall << " precision=" << result.classical.precision
                  << " fpr=" << result.classical.fpr << " median_px=" << result.classical.localization.median_px
                  << " p95_px=" << result.classical.localization.p95_px
                  << " max_px=" << result.classical.localization.max_px << " gt50=" << result.classical.outliers.gt_50
                  << "\n"
                  << "  AI        recall=" << result.ai.recall << " precision=" << result.ai.precision
                  << " fpr=" << result.ai.fpr << " median_px=" << result.ai.localization.median_px
                  << " p95_px=" << result.ai.localization.p95_px << " max_px=" << result.ai.localization.max_px
                  << " gt50=" << result.ai.outliers.gt_50 << "\n"
                  << "  HYBRID    recall=" << result.hybrid.recall << " precision=" << result.hybrid.precision
                  << " fpr=" << result.hybrid.fpr << " median_px=" << result.hybrid.localization.median_px
                  << " p95_px=" << result.hybrid.localization.p95_px
                  << " max_px=" << result.hybrid.localization.max_px << " gt50=" << result.hybrid.outliers.gt_50
                  << "  [agree=" << result.hybrid_sources.hybrid_agreement
                  << " classical=" << result.hybrid_sources.classical
                  << " ai_only=" << result.hybrid_sources.ai_only_unverified
                  << " disagree=" << result.hybrid_sources.detector_disagreement
                  << " none=" << result.hybrid_sources.none_not_applicable << "]\n"
                  << "  safety: disagreement_frames_with_target=" << result.safety.disagreement_frames_with_target_present
                  << " would_be_bad(>20/50/100)=" << result.safety.disagreement_would_be_bad_gt20 << "/"
                  << result.safety.disagreement_would_be_bad_gt50 << "/" << result.safety.disagreement_would_be_bad_gt100
                  << " ai_only_fp_withheld=" << result.safety.ai_only_false_positive_withheld << "\n";
    }

    const auto overall_c = overall_classical.finalize();
    const auto overall_a = overall_ai.finalize();
    const auto overall_h = overall_hybrid.finalize();
    write_common_row("OVERALL", "CLASSICAL", overall_c);
    write_common_row("OVERALL", "AI", overall_a);
    write_common_row("OVERALL", "HYBRID", overall_h);
    common_csv.close();

    std::cout << "\n--- COMMON-FRAME OVERALL (all " << stage4::kAllScenarios.size() << " scenarios pooled) ---\n"
              << std::fixed << std::setprecision(4) << "CLASSICAL: accepted_rate=" << overall_c.accepted_rate
              << " recall=" << overall_c.recall << " precision=" << overall_c.precision << " fpr=" << overall_c.fpr
              << " median_px=" << overall_c.localization.median_px << " mae_px=" << overall_c.localization.mae_px
              << " rmse_px=" << overall_c.localization.rmse_px << " p95_px=" << overall_c.localization.p95_px
              << " max_px=" << overall_c.localization.max_px << "\n"
              << "AI:        accepted_rate=" << overall_a.accepted_rate << " recall=" << overall_a.recall
              << " precision=" << overall_a.precision << " fpr=" << overall_a.fpr
              << " median_px=" << overall_a.localization.median_px << " mae_px=" << overall_a.localization.mae_px
              << " rmse_px=" << overall_a.localization.rmse_px << " p95_px=" << overall_a.localization.p95_px
              << " max_px=" << overall_a.localization.max_px << "\n"
              << "HYBRID:    accepted_rate=" << overall_h.accepted_rate << " recall=" << overall_h.recall
              << " precision=" << overall_h.precision << " fpr=" << overall_h.fpr
              << " median_px=" << overall_h.localization.median_px << " mae_px=" << overall_h.localization.mae_px
              << " rmse_px=" << overall_h.localization.rmse_px << " p95_px=" << overall_h.localization.p95_px
              << " max_px=" << overall_h.localization.max_px << "\n"
              << "SEVERE OUTLIERS (>20/>50/>100/max): CLASSICAL " << overall_c.outliers.gt_20 << "/"
              << overall_c.outliers.gt_50 << "/" << overall_c.outliers.gt_100 << "/" << overall_c.localization.max_px
              << "   AI " << overall_a.outliers.gt_20 << "/" << overall_a.outliers.gt_50 << "/"
              << overall_a.outliers.gt_100 << "/" << overall_a.localization.max_px << "   HYBRID "
              << overall_h.outliers.gt_20 << "/" << overall_h.outliers.gt_50 << "/" << overall_h.outliers.gt_100
              << "/" << overall_h.localization.max_px << "\n";

    // ---------------- B. Closed-loop benchmark ----------------
    std::cout << "\n=== B. CLOSED-LOOP BENCHMARK (" << closed_loop_duration_s << "s @ 50Hz x "
              << stage4::kStage4BaseSeeds.size() << " seeds) ===\n";

    stage4::ClosedLoopBenchConfig cl_bench{};
    cl_bench.duration_s = closed_loop_duration_s;
    cl_bench.dt_s = 0.02;
    cl_bench.warmup_frames_for_latency = opt.quick ? 2 : 20;

    const std::array<PerceptionMode, 3> modes = {PerceptionMode::Classical, PerceptionMode::AI, PerceptionMode::Hybrid};

    std::ofstream cl_csv(opt.out_dir + "/csv/closed_loop.csv");
    cl_csv << "scenario,mode,total_frames,accepted_detection_fraction,target_lost_frames,longest_loss_streak,"
              "reacquisition_count,mean_reacquisition_time_frames,rms_angular_error_deg,median_angular_error_deg,"
              "p95_angular_error_deg,max_angular_error_deg,saturated_actuator_frames,control_outlier_gt20,"
              "control_outlier_gt50,control_outlier_gt100,max_control_error_px,latency_mean_ms,latency_p95_ms,"
              "hybrid_agreement,hybrid_classical,hybrid_ai_only,hybrid_disagreement,hybrid_none\n";

    constexpr double kRadToDeg = 180.0 / std::numbers::pi_v<double>;
    const auto write_cl_row = [&cl_csv](const std::string& scenario, const stage4::ClosedLoopScenarioModeResult& r) {
        cl_csv << scenario << ',' << mode_name(r.mode) << ',' << r.total_frames << ','
               << r.accepted_detection_fraction << ',' << r.target_lost_frames << ',' << r.longest_loss_streak << ','
               << r.reacquisition_count << ',' << r.mean_reacquisition_time_frames << ','
               << r.rms_angular_error_rad * kRadToDeg << ',' << r.median_angular_error_rad * kRadToDeg << ','
               << r.p95_angular_error_rad * kRadToDeg << ',' << r.max_angular_error_rad * kRadToDeg << ','
               << r.saturated_actuator_frames << ',' << r.control_outlier_gt20 << ',' << r.control_outlier_gt50
               << ',' << r.control_outlier_gt100 << ',' << r.max_control_error_px << ','
               << r.perception_latency_mean_ms << ',' << r.perception_latency_p95_ms << ','
               << r.hybrid_sources.hybrid_agreement << ',' << r.hybrid_sources.classical << ','
               << r.hybrid_sources.ai_only_unverified << ',' << r.hybrid_sources.detector_disagreement << ','
               << r.hybrid_sources.none_not_applicable << '\n';
    };

    std::vector<stage4::ClosedLoopScenarioModeResult> closed_loop_results;
    std::array<std::vector<stage4::ClosedLoopRawSeedResult>, 3> overall_raw_by_mode;

    for (const auto scenario : stage4::kAllScenarios) {
        const std::string token(stage4::scenario_token(scenario));
        std::cout << "[" << token << "] " << stage4::scenario_name(scenario) << "\n";
        for (std::size_t m = 0; m < modes.size(); ++m) {
            const PerceptionMode mode = modes[m];
            std::vector<stage4::ClosedLoopRawSeedResult> per_seed;
            for (std::size_t s = 0; s < stage4::kStage4BaseSeeds.size(); ++s) {
                per_seed.push_back(stage4::run_closed_loop_scenario_mode_seed(
                    scenario, mode, stage4::kStage4BaseSeeds[s], s, cl_bench, classical_detector, *ai_detector));
            }
            const auto finalized = stage4::finalize_closed_loop_results(scenario, mode, per_seed);
            closed_loop_results.push_back(finalized);
            write_cl_row(token, finalized);
            overall_raw_by_mode[m].insert(overall_raw_by_mode[m].end(), per_seed.begin(), per_seed.end());

            std::cout << std::fixed << std::setprecision(4) << "  " << mode_name(mode)
                      << ": accepted=" << finalized.accepted_detection_fraction
                      << " lost=" << finalized.target_lost_frames << " longest_loss=" << finalized.longest_loss_streak
                      << " reacq=" << finalized.reacquisition_count
                      << " RMS_deg=" << finalized.rms_angular_error_rad * kRadToDeg
                      << " P95_deg=" << finalized.p95_angular_error_rad * kRadToDeg
                      << " max_deg=" << finalized.max_angular_error_rad * kRadToDeg
                      << " sat=" << finalized.saturated_actuator_frames
                      << " ctrl_outlier(>20/50/100)=" << finalized.control_outlier_gt20 << "/"
                      << finalized.control_outlier_gt50 << "/" << finalized.control_outlier_gt100
                      << " lat_mean_ms=" << finalized.perception_latency_mean_ms
                      << " lat_p95_ms=" << finalized.perception_latency_p95_ms;
            if (mode == PerceptionMode::Hybrid) {
                std::cout << " [agree=" << finalized.hybrid_sources.hybrid_agreement
                          << " classical=" << finalized.hybrid_sources.classical
                          << " ai_only=" << finalized.hybrid_sources.ai_only_unverified
                          << " disagree=" << finalized.hybrid_sources.detector_disagreement
                          << " none=" << finalized.hybrid_sources.none_not_applicable << "]";
            }
            std::cout << "\n";
        }
    }
    cl_csv.close();

    std::cout << "\n--- CLOSED-LOOP OVERALL (all " << stage4::kAllScenarios.size() << " scenarios pooled) ---\n";
    for (std::size_t m = 0; m < modes.size(); ++m) {
        const auto overall = stage4::finalize_closed_loop_results(stage4::ScenarioId::Clean, modes[m], overall_raw_by_mode[m]);
        std::cout << std::fixed << std::setprecision(4) << mode_name(modes[m])
                  << ": accepted=" << overall.accepted_detection_fraction << " lost=" << overall.target_lost_frames
                  << " longest_loss=" << overall.longest_loss_streak << " reacq=" << overall.reacquisition_count
                  << " RMS_deg=" << overall.rms_angular_error_rad * kRadToDeg
                  << " median_deg=" << overall.median_angular_error_rad * kRadToDeg
                  << " P95_deg=" << overall.p95_angular_error_rad * kRadToDeg
                  << " max_deg=" << overall.max_angular_error_rad * kRadToDeg << " sat=" << overall.saturated_actuator_frames
                  << " ctrl_outlier(>20/50/100)=" << overall.control_outlier_gt20 << "/" << overall.control_outlier_gt50
                  << "/" << overall.control_outlier_gt100 << " lat_mean_ms=" << overall.perception_latency_mean_ms
                  << " lat_p95_ms=" << overall.perception_latency_p95_ms << "\n";
        if (modes[m] == PerceptionMode::Hybrid) {
            std::cout << "  hybrid sources: agree=" << overall.hybrid_sources.hybrid_agreement
                      << " classical=" << overall.hybrid_sources.classical
                      << " ai_only=" << overall.hybrid_sources.ai_only_unverified
                      << " disagree=" << overall.hybrid_sources.detector_disagreement
                      << " none=" << overall.hybrid_sources.none_not_applicable << "\n";
        }
    }

    // ---------------- Per-scenario markdown reports ----------------
    for (const auto scenario : stage4::kAllScenarios) {
        const std::string token(stage4::scenario_token(scenario));
        const std::string name(stage4::scenario_name(scenario));
        const auto cf = std::find_if(
            common_frame_results.begin(), common_frame_results.end(),
            [scenario](const auto& r) { return r.scenario == scenario; });

        std::ofstream report(opt.out_dir + "/reports/" + token + "_" + name + ".md");
        report << "# Scenario " << token << " — " << name << "\n\n";
        report << "## Common-frame benchmark\n\n";
        report << "| mode | accepted_rate | recall | precision | fpr | median_px | p95_px | max_px | gt20 | gt50 | gt100 |\n";
        report << "|---|---|---|---|---|---|---|---|---|---|---|\n";
        if (cf != common_frame_results.end()) {
            const auto row = [&report](const std::string& mode, const stage4::PerceptionRateStats& s) {
                report << "| " << mode << " | " << s.accepted_rate << " | " << s.recall << " | " << s.precision
                       << " | " << s.fpr << " | " << s.localization.median_px << " | " << s.localization.p95_px
                       << " | " << s.localization.max_px << " | " << s.outliers.gt_20 << " | " << s.outliers.gt_50
                       << " | " << s.outliers.gt_100 << " |\n";
            };
            row("CLASSICAL", cf->classical);
            row("AI", cf->ai);
            row("HYBRID", cf->hybrid);
            report << "\nHybrid sources: agree=" << cf->hybrid_sources.hybrid_agreement
                   << " classical=" << cf->hybrid_sources.classical
                   << " ai_only=" << cf->hybrid_sources.ai_only_unverified
                   << " disagree=" << cf->hybrid_sources.detector_disagreement
                   << " none=" << cf->hybrid_sources.none_not_applicable << "\n\n";
            report << "Offline safety: disagreement_frames_with_target_present="
                   << cf->safety.disagreement_frames_with_target_present << ", would_be_bad(>20/50/100)="
                   << cf->safety.disagreement_would_be_bad_gt20 << "/" << cf->safety.disagreement_would_be_bad_gt50
                   << "/" << cf->safety.disagreement_would_be_bad_gt100 << ", ai_only_false_positive_withheld="
                   << cf->safety.ai_only_false_positive_withheld << "\n\n";
        }

        report << "## Closed-loop benchmark\n\n";
        report << "| mode | accepted_frac | lost | longest_loss | reacq | RMS_deg | P95_deg | max_deg | sat | "
                  "ctrl_gt20 | ctrl_gt50 | ctrl_gt100 | lat_mean_ms | lat_p95_ms |\n";
        report << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
        for (const auto& r : closed_loop_results) {
            if (r.scenario != scenario) {
                continue;
            }
            report << "| " << mode_name(r.mode) << " | " << r.accepted_detection_fraction << " | "
                   << r.target_lost_frames << " | " << r.longest_loss_streak << " | " << r.reacquisition_count
                   << " | " << r.rms_angular_error_rad * kRadToDeg << " | " << r.p95_angular_error_rad * kRadToDeg
                   << " | " << r.max_angular_error_rad * kRadToDeg << " | " << r.saturated_actuator_frames << " | "
                   << r.control_outlier_gt20 << " | " << r.control_outlier_gt50 << " | " << r.control_outlier_gt100
                   << " | " << r.perception_latency_mean_ms << " | " << r.perception_latency_p95_ms << " |\n";
        }
    }

    // ---------------- JSON summary ----------------
    {
        std::ofstream json_out(opt.out_dir + "/stage4_summary.json");
        JsonWriter j(json_out);
        j.begin_object();
        j.key("protocol"); j.value("docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md");
        j.key("quick_smoke_test"); j.value(opt.quick);
        j.key("common_frame_frames_per_seed"); j.value(frames_per_seed);
        j.key("closed_loop_duration_s"); j.value(closed_loop_duration_s);

        j.key("common_frame_overall");
        j.begin_object();
        j.key("classical"); write_perception_rate_json(j, overall_c);
        j.key("ai"); write_perception_rate_json(j, overall_a);
        j.key("hybrid"); write_perception_rate_json(j, overall_h);
        j.end_object();

        j.key("common_frame_by_scenario");
        j.begin_array();
        for (const auto& r : common_frame_results) {
            j.begin_object();
            j.key("scenario"); j.value(std::string(stage4::scenario_token(r.scenario)));
            j.key("name"); j.value(std::string(stage4::scenario_name(r.scenario)));
            j.key("classical"); write_perception_rate_json(j, r.classical);
            j.key("ai"); write_perception_rate_json(j, r.ai);
            j.key("hybrid"); write_perception_rate_json(j, r.hybrid);
            j.key("hybrid_sources"); write_hybrid_sources_json(j, r.hybrid_sources);
            j.key("safety");
            j.begin_object();
            j.key("disagreement_frames_with_target_present"); j.value(r.safety.disagreement_frames_with_target_present);
            j.key("disagreement_would_be_bad_gt20"); j.value(r.safety.disagreement_would_be_bad_gt20);
            j.key("disagreement_would_be_bad_gt50"); j.value(r.safety.disagreement_would_be_bad_gt50);
            j.key("disagreement_would_be_bad_gt100"); j.value(r.safety.disagreement_would_be_bad_gt100);
            j.key("ai_only_false_positive_withheld"); j.value(r.safety.ai_only_false_positive_withheld);
            j.end_object();
            j.end_object();
        }
        j.end_array();

        j.key("closed_loop_by_scenario_mode");
        j.begin_array();
        for (const auto& r : closed_loop_results) {
            j.begin_object();
            j.key("scenario"); j.value(std::string(stage4::scenario_token(r.scenario)));
            j.key("mode"); j.value(std::string(mode_name(r.mode)));
            j.key("metrics"); write_closed_loop_json(j, r);
            j.end_object();
        }
        j.end_array();

        j.end_object();
    }

    // ---------------- Curated evidence ----------------
    std::cout << "\n=== CURATED EVIDENCE ===\n";
    capture_curated_evidence(opt.out_dir + "/evidence", classical_detector, *ai_detector);

    std::cout << "\nSTAGE 4 EVALUATION COMPLETE. Artifacts: " << opt.out_dir << "/\n";
    return 0;
}
