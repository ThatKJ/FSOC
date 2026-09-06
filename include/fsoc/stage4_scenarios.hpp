#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "fsoc/ai_frame_synth.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/stage4_degradation.hpp"

namespace fsoc::stage4 {

// ---------------------------------------------------------------------------
// Stage-4 scenario definitions (FROZEN — docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md §5)
// ---------------------------------------------------------------------------
//
// Every value returned by the functions below is transcribed exactly from the
// frozen protocol document. Do not edit either without updating the other in
// the same commit, and never after looking at a Stage-4 result.

enum class ScenarioId {
    Clean,
    LowSnr,
    StarClutter,
    HotPixels,
    BrightDistractor,
    Blur,
    MotionBlur,
    BackgroundGradient,
    TargetAbsent,
    MixedRandomized,
    AdversarialAmbiguity,
};

inline constexpr std::array<ScenarioId, 11> kAllScenarios = {
    ScenarioId::Clean,        ScenarioId::LowSnr,          ScenarioId::StarClutter,
    ScenarioId::HotPixels,    ScenarioId::BrightDistractor, ScenarioId::Blur,
    ScenarioId::MotionBlur,   ScenarioId::BackgroundGradient, ScenarioId::TargetAbsent,
    ScenarioId::MixedRandomized, ScenarioId::AdversarialAmbiguity,
};

[[nodiscard]] std::string_view scenario_token(ScenarioId id);  // "A" .. "K"
[[nodiscard]] std::string_view scenario_name(ScenarioId id);   // "CLEAN", "LOW_SNR", ...
[[nodiscard]] int scenario_index(ScenarioId id);                // 0..10, table order

// Frozen Stage-4-exclusive seeds (protocol §4). Disjoint from the Stage-1/2
// dataset seed (26169) and the Stage-3 parity fixture.
inline constexpr std::array<std::uint64_t, 5> kStage4BaseSeeds = {
    410101ULL, 410102ULL, 410103ULL, 410104ULL, 410105ULL};

// scenario_stream_seed / frame_seed reuse the existing, already-tested Stage-1
// utilities (fsoc::ai::splitmix64, fsoc::ai::sample_seed_for) — no new RNG
// mechanism. Random degradation depends ONLY on (scenario, base_seed,
// frame_index) — never on detector output, controller output, or
// accepted/rejected status (protocol §3.2, §4).
[[nodiscard]] std::uint64_t scenario_stream_seed(int scenario_idx, std::uint64_t base_seed);
[[nodiscard]] std::uint64_t frame_seed(int scenario_idx, std::uint64_t base_seed, std::uint64_t frame_index);

// Common-frame benchmark: the AiFrameSynthesizer config for this scenario.
[[nodiscard]] fsoc::ai::AiFrameSynthConfig common_frame_config(ScenarioId id);

// Closed-loop benchmark: the (frozen, unmodified) renderer config, the
// post-process degradation config, and the fixed target world position for
// this scenario. `seed_index` (0-based index into kStage4BaseSeeds) is used
// only by scenario F, which alternates GaussianBlur/Defocus by seed-index
// parity (fixed at freeze time, not chosen from results).
[[nodiscard]] fsoc::RendererConfig closed_loop_renderer_config(ScenarioId id);
[[nodiscard]] DegradationConfig closed_loop_degradation_config(ScenarioId id, std::size_t seed_index);
[[nodiscard]] fsoc::Vec3 closed_loop_target_position_m(ScenarioId id);

}  // namespace fsoc::stage4
