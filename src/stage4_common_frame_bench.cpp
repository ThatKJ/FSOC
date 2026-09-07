#include "fsoc/stage4_common_frame_bench.hpp"

#include <cmath>
#include <optional>

#include "fsoc/perception.hpp"

namespace fsoc::stage4 {

namespace {

[[nodiscard]] double euclid(const double ax, const double ay, const double bx, const double by) {
    return std::hypot(ax - bx, ay - by);
}

}  // namespace

CommonFrameScenarioResult run_common_frame_scenario(
    const ScenarioId scenario,
    const std::array<std::uint64_t, 5>& base_seeds,
    const std::size_t frames_per_seed,
    const fsoc::BeaconDetector& classical_detector,
    const fsoc::AiBeaconDetector& ai_detector,
    PerceptionRateAccumulator* const global_classical,
    PerceptionRateAccumulator* const global_ai,
    PerceptionRateAccumulator* const global_hybrid) {
    const fsoc::ai::AiFrameSynthConfig config = common_frame_config(scenario);
    const fsoc::ai::AiFrameSynthesizer synth{config};
    const int idx = scenario_index(scenario);
    const bool force_absent = (scenario == ScenarioId::TargetAbsent);

    PerceptionRateAccumulator classical_acc;
    PerceptionRateAccumulator ai_acc;
    PerceptionRateAccumulator hybrid_acc;
    HybridSourceCounts sources{};
    DisagreementSafetyStats safety{};
    std::size_t total_frames = 0;

    for (const std::uint64_t base_seed : base_seeds) {
        for (std::size_t i = 0; i < frames_per_seed; ++i) {
            const std::uint64_t seed = frame_seed(idx, base_seed, static_cast<std::uint64_t>(i));
            const fsoc::ai::SynthFrame sample =
                force_absent ? synth.synthesize(seed, /*force_target=*/false) : synth.synthesize(seed);
            ++total_frames;

            const bool truth_present = sample.target_present;
            const std::optional<fsoc::BeaconDetection> classical = classical_detector.detect(sample.image);
            const std::optional<fsoc::AiBeaconDetection> ai = ai_detector.detect(sample.image);

            // --- Classical row ---
            {
                const bool accepted = classical.has_value();
                std::optional<double> err;
                if (accepted && truth_present) {
                    err = euclid(
                        classical->centroid_px.x_px, classical->centroid_px.y_px, sample.x_px, sample.y_px);
                }
                classical_acc.add_frame(truth_present, accepted, err);
                if (global_classical != nullptr) {
                    global_classical->add_frame(truth_present, accepted, err);
                }
            }

            // --- AI row ---
            {
                const bool accepted = ai.has_value();
                std::optional<double> err;
                if (accepted && truth_present) {
                    err = euclid(
                        ai->detection.centroid_px.x_px, ai->detection.centroid_px.y_px, sample.x_px,
                        sample.y_px);
                }
                ai_acc.add_frame(truth_present, accepted, err);
                if (global_ai != nullptr) {
                    global_ai->add_frame(truth_present, accepted, err);
                }
            }

            // --- Hybrid row: the real, unmodified resolve_perception() ---
            {
                const fsoc::PerceptionResult r = fsoc::resolve_perception(fsoc::PerceptionMode::Hybrid, classical, ai);
                const bool accepted = r.detection.has_value();
                std::optional<double> err;
                if (accepted && truth_present) {
                    err = euclid(
                        r.detection->centroid_px.x_px, r.detection->centroid_px.y_px, sample.x_px, sample.y_px);
                }
                hybrid_acc.add_frame(truth_present, accepted, err);
                if (global_hybrid != nullptr) {
                    global_hybrid->add_frame(truth_present, accepted, err);
                }

                switch (r.diagnostics.perception_source) {
                    case fsoc::PerceptionSource::HybridAgreement:
                        ++sources.hybrid_agreement;
                        break;
                    case fsoc::PerceptionSource::Classical:
                        ++sources.classical;
                        break;
                    case fsoc::PerceptionSource::AI:
                        // Never emitted under Hybrid (ADR-018) — not tallied as a distinct case here.
                        break;
                    case fsoc::PerceptionSource::None:
                        switch (r.diagnostics.rejection_reason) {
                            case fsoc::PerceptionRejectionReason::AiOnlyUnverified:
                                ++sources.ai_only_unverified;
                                break;
                            case fsoc::PerceptionRejectionReason::DetectorDisagreement:
                                ++sources.detector_disagreement;
                                break;
                            case fsoc::PerceptionRejectionReason::NotApplicable:
                                ++sources.none_not_applicable;
                                break;
                        }
                        break;
                }

                // Offline safety scoring (truth used here only — never fed to resolve_perception).
                if (r.diagnostics.rejection_reason == fsoc::PerceptionRejectionReason::DetectorDisagreement &&
                    truth_present && ai.has_value()) {
                    ++safety.disagreement_frames_with_target_present;
                    const double ai_err =
                        euclid(ai->detection.centroid_px.x_px, ai->detection.centroid_px.y_px, sample.x_px,
                               sample.y_px);
                    if (ai_err > 20.0) ++safety.disagreement_would_be_bad_gt20;
                    if (ai_err > 50.0) ++safety.disagreement_would_be_bad_gt50;
                    if (ai_err > 100.0) ++safety.disagreement_would_be_bad_gt100;
                }
                if (r.diagnostics.rejection_reason == fsoc::PerceptionRejectionReason::AiOnlyUnverified &&
                    !truth_present) {
                    ++safety.ai_only_false_positive_withheld;
                }
            }
        }
    }

    CommonFrameScenarioResult result{};
    result.scenario = scenario;
    result.total_frames = total_frames;
    result.classical = classical_acc.finalize();
    result.ai = ai_acc.finalize();
    result.hybrid = hybrid_acc.finalize();
    result.hybrid_sources = sources;
    result.safety = safety;
    return result;
}

}  // namespace fsoc::stage4
