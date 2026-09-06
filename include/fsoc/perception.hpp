#pragma once

#include <optional>
#include <string_view>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/measurement.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// Perception seam (Stage 3) — CLASSICAL / AI / Safe HYBRID (ADR-018)
// ---------------------------------------------------------------------------
//
// resolve_perception() is a pure decision function: given the CLASSICAL and
// AI detector outputs already computed for one frame (both pixels-only
// estimates), it decides the control-facing std::optional<BeaconDetection>
// and reports why. It receives NO world truth: no TargetState, no
// CameraObservation, no exact Projection, no TrackingError, no controller
// state. See docs/19_AI_PERCEPTION_ARCHITECTURE.md §5 and DECISIONS.md
// ADR-018, which this function implements exactly (case numbers below match
// that table 1:1).

enum class PerceptionMode { Classical, AI, Hybrid };

// DIAGNOSTIC ONLY — distinct from TrackingState / DemoRunState; never read by
// the PID.
enum class PerceptionSource { None, Classical, AI, HybridAgreement };

// DIAGNOSTIC ONLY — meaningful iff the control-facing result is std::nullopt
// under PerceptionMode::Hybrid.
enum class PerceptionRejectionReason { NotApplicable, AiOnlyUnverified, DetectorDisagreement };

// Canonical SCREAMING_SNAKE names for telemetry / CSV / the frontend contract
// (matches DemoTrackingState::to_string()'s convention). Never used for control logic.
[[nodiscard]] std::string_view to_string(PerceptionMode mode) noexcept;
[[nodiscard]] std::string_view to_string(PerceptionSource source) noexcept;
[[nodiscard]] std::string_view to_string(PerceptionRejectionReason reason) noexcept;

// Frozen (ADR-018): the source-space size of one TinyBeaconNet heatmap cell
// (INPUT_STRIDE = 8; 640/80 = 480/60 = 8). This is an engineering geometry
// constant, NOT an ML confidence threshold, and is not to be tuned against
// future closed-loop (Stage-4) results.
inline constexpr double kAgreementRadiusPx = 8.0;

struct PerceptionDiagnostics {
    PerceptionMode perception_mode{PerceptionMode::Classical};
    PerceptionSource perception_source{PerceptionSource::None};
    bool classical_detected{false};
    bool ai_candidate_detected{false};
    std::optional<double> ai_presence_probability{};
    std::optional<double> ai_inference_ms{};
    std::optional<double> classical_ai_distance_px{};
    PerceptionRejectionReason rejection_reason{PerceptionRejectionReason::NotApplicable};
};

struct PerceptionResult {
    std::optional<BeaconDetection> detection{};  // control-facing; feeds compute_tracking_error
    PerceptionDiagnostics diagnostics{};
};

// Mode dispatch + the Safe Hybrid table (ADR-018 cases 1-5) from already-
// computed classical / AI candidates:
//
//   Classical -> control-facing = classical_detection, unchanged (v1 behaviour).
//   AI        -> control-facing = ai_detection's BeaconDetection (diagnostic/
//                benchmark mode only — NOT the safe default).
//   Hybrid    -> case 1 (agree, distance <= kAgreementRadiusPx): classical
//                  centroid, source HybridAgreement;
//                case 2 (classical only): classical centroid, source Classical;
//                case 3 (AI only): std::nullopt, reason AiOnlyUnverified;
//                case 4 (disagree, distance > kAgreementRadiusPx): std::nullopt,
//                  reason DetectorDisagreement — never averaged, never an AI
//                  confidence override;
//                case 5 (neither): std::nullopt, reason NotApplicable.
//
// Never receives (and does not need) TargetState, trajectory truth, or a
// projected ImagePoint — only the two detectors' own pixel-space outputs.
[[nodiscard]] PerceptionResult resolve_perception(
    PerceptionMode mode,
    const std::optional<BeaconDetection>& classical_detection,
    const std::optional<AiBeaconDetection>& ai_detection);

}  // namespace fsoc
