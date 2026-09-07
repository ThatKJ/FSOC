#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/stage4_metrics.hpp"
#include "fsoc/stage4_scenarios.hpp"

namespace fsoc::stage4 {

// Per-frame PerceptionSource / PerceptionRejectionReason tallies for the
// Hybrid row of one scenario (protocol §7/§8).
struct HybridSourceCounts {
    std::size_t hybrid_agreement{0};
    std::size_t classical{0};
    std::size_t ai_only_unverified{0};
    std::size_t detector_disagreement{0};
    std::size_t none_not_applicable{0};
};

// Offline, truth-scored safety metric (protocol §7) — truth is used ONLY here,
// never inside resolve_perception().
struct DisagreementSafetyStats {
    std::size_t disagreement_frames_with_target_present{0};
    std::size_t disagreement_would_be_bad_gt20{0};
    std::size_t disagreement_would_be_bad_gt50{0};
    std::size_t disagreement_would_be_bad_gt100{0};
    std::size_t ai_only_false_positive_withheld{0};  // AiOnlyUnverified frames where truth says absent
};

struct CommonFrameScenarioResult {
    ScenarioId scenario{};
    std::size_t total_frames{0};
    PerceptionRateStats classical{};
    PerceptionRateStats ai{};
    PerceptionRateStats hybrid{};
    HybridSourceCounts hybrid_sources{};
    DisagreementSafetyStats safety{};
};

// Runs the common-frame benchmark for one scenario across every base seed
// (protocol §4/§6): identical frames scored by Classical, AI, and Hybrid — the
// Hybrid row reuses the SAME per-frame Classical + AI outputs (no extra
// inference) through the real, unmodified resolve_perception(). Truth
// (`SynthFrame::target_present/x_px/y_px`) is used ONLY by this evaluator to
// score outputs — it never reaches `classical_detector`, `ai_detector`, or
// `resolve_perception()`.
//
// `global_classical`/`global_ai`/`global_hybrid`, if non-null, also receive
// every frame this call scores (in addition to the scenario-local
// accumulators reflected in the returned result) — the caller can pass the
// same three accumulators across every scenario to get an EXACT pooled
// "common-frame overall" result (correct median/RMSE/P95/max over every
// frame, not an average of per-scenario statistics).
[[nodiscard]] CommonFrameScenarioResult run_common_frame_scenario(
    ScenarioId scenario,
    const std::array<std::uint64_t, 5>& base_seeds,
    std::size_t frames_per_seed,
    const fsoc::BeaconDetector& classical_detector,
    const fsoc::AiBeaconDetector& ai_detector,
    PerceptionRateAccumulator* global_classical = nullptr,
    PerceptionRateAccumulator* global_ai = nullptr,
    PerceptionRateAccumulator* global_hybrid = nullptr);

}  // namespace fsoc::stage4
