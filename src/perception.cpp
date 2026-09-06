#include "fsoc/perception.hpp"

#include <cmath>

namespace fsoc {

namespace {

[[nodiscard]] double centroid_distance_px(const BeaconDetection& a, const BeaconDetection& b) noexcept {
    const double dx = a.centroid_px.x_px - b.centroid_px.x_px;
    const double dy = a.centroid_px.y_px - b.centroid_px.y_px;
    return std::hypot(dx, dy);
}

}  // namespace

std::string_view to_string(const PerceptionMode mode) noexcept {
    switch (mode) {
        case PerceptionMode::Classical: return "CLASSICAL";
        case PerceptionMode::AI: return "AI";
        case PerceptionMode::Hybrid: return "HYBRID";
    }
    return "CLASSICAL";
}

std::string_view to_string(const PerceptionSource source) noexcept {
    switch (source) {
        case PerceptionSource::None: return "NONE";
        case PerceptionSource::Classical: return "CLASSICAL";
        case PerceptionSource::AI: return "AI";
        case PerceptionSource::HybridAgreement: return "HYBRID_AGREEMENT";
    }
    return "NONE";
}

std::string_view to_string(const PerceptionRejectionReason reason) noexcept {
    switch (reason) {
        case PerceptionRejectionReason::NotApplicable: return "NOT_APPLICABLE";
        case PerceptionRejectionReason::AiOnlyUnverified: return "AI_ONLY_UNVERIFIED";
        case PerceptionRejectionReason::DetectorDisagreement: return "DETECTOR_DISAGREEMENT";
    }
    return "NOT_APPLICABLE";
}

PerceptionResult resolve_perception(
    const PerceptionMode mode,
    const std::optional<BeaconDetection>& classical_detection,
    const std::optional<AiBeaconDetection>& ai_detection) {
    PerceptionResult result{};
    result.diagnostics.perception_mode = mode;

    switch (mode) {
        case PerceptionMode::Classical: {
            // Classical mode ignores any AI candidate entirely: bit-identical
            // to the pre-Stage-3 control-facing behaviour.
            result.diagnostics.classical_detected = classical_detection.has_value();
            result.detection = classical_detection;
            result.diagnostics.perception_source =
                classical_detection.has_value() ? PerceptionSource::Classical : PerceptionSource::None;
            result.diagnostics.rejection_reason = PerceptionRejectionReason::NotApplicable;
            return result;
        }
        case PerceptionMode::AI: {
            // Explicit, non-default diagnostic/benchmark mode (docs/19 §5): the
            // thresholded AI candidate IS exposed as the detector output here.
            result.diagnostics.ai_candidate_detected = ai_detection.has_value();
            if (ai_detection.has_value()) {
                result.diagnostics.ai_presence_probability = ai_detection->confidence;
                result.diagnostics.ai_inference_ms = ai_detection->inference_ms;
                result.detection = ai_detection->detection;
            }
            result.diagnostics.perception_source =
                ai_detection.has_value() ? PerceptionSource::AI : PerceptionSource::None;
            result.diagnostics.rejection_reason = PerceptionRejectionReason::NotApplicable;
            return result;
        }
        case PerceptionMode::Hybrid: {
            const bool classical_ok = classical_detection.has_value();
            const bool ai_ok = ai_detection.has_value();
            result.diagnostics.classical_detected = classical_ok;
            result.diagnostics.ai_candidate_detected = ai_ok;
            if (ai_ok) {
                result.diagnostics.ai_presence_probability = ai_detection->confidence;
                result.diagnostics.ai_inference_ms = ai_detection->inference_ms;
            }

            if (classical_ok && ai_ok) {
                // Case 1 / Case 4.
                const double distance =
                    centroid_distance_px(*classical_detection, ai_detection->detection);
                result.diagnostics.classical_ai_distance_px = distance;
                if (distance <= kAgreementRadiusPx) {
                    result.detection = classical_detection;  // best clean sub-pixel precision
                    result.diagnostics.perception_source = PerceptionSource::HybridAgreement;
                    result.diagnostics.rejection_reason = PerceptionRejectionReason::NotApplicable;
                } else {
                    result.detection = std::nullopt;  // unconditional reject — no override, no average
                    result.diagnostics.perception_source = PerceptionSource::None;
                    result.diagnostics.rejection_reason = PerceptionRejectionReason::DetectorDisagreement;
                }
            } else if (classical_ok) {
                // Case 2.
                result.detection = classical_detection;
                result.diagnostics.perception_source = PerceptionSource::Classical;
                result.diagnostics.rejection_reason = PerceptionRejectionReason::NotApplicable;
            } else if (ai_ok) {
                // Case 3 — AI-only never gets control authority (ADR-018).
                result.detection = std::nullopt;
                result.diagnostics.perception_source = PerceptionSource::None;
                result.diagnostics.rejection_reason = PerceptionRejectionReason::AiOnlyUnverified;
            } else {
                // Case 5.
                result.detection = std::nullopt;
                result.diagnostics.perception_source = PerceptionSource::None;
                result.diagnostics.rejection_reason = PerceptionRejectionReason::NotApplicable;
            }
            return result;
        }
    }
    return result;  // unreachable for a valid PerceptionMode
}

}  // namespace fsoc
