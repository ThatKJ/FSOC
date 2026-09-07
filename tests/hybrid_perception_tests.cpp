// Stage 3 — Safe Hybrid policy (ADR-018) unit tests.
//
// resolve_perception() is a pure decision function over already-computed
// classical / AI candidates: no cv::Mat, no model, no filesystem. These tests
// construct BeaconDetection / AiBeaconDetection values directly and check the
// ADR-018 decision table (docs/19_AI_PERCEPTION_ARCHITECTURE.md §5) exactly,
// including the frozen 8.0 px agreement-radius boundary.
//
// Same lightweight harness as tests/step1_tests.cpp .. tests/step5_tests.cpp.

#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "fsoc/measurement.hpp"
#include "fsoc/perception.hpp"

namespace {

using fsoc::AiBeaconDetection;
using fsoc::BeaconDetection;
using fsoc::ImagePoint;
using fsoc::PerceptionMode;
using fsoc::PerceptionRejectionReason;
using fsoc::PerceptionResult;
using fsoc::PerceptionSource;
using fsoc::resolve_perception;

int failures = 0;

void check(const bool condition, const std::string_view expression, const int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

// ---- helpers -----------------------------------------------------

[[nodiscard]] BeaconDetection classical_at(const double x, const double y) {
    return BeaconDetection{.centroid_px = {.x_px = x, .y_px = y}};
}

[[nodiscard]] AiBeaconDetection ai_at(
    const double x, const double y, const double confidence = 0.97, const double peak = 0.97,
    const double inference_ms = 1.5) {
    return AiBeaconDetection{
        .detection = classical_at(x, y),
        .confidence = confidence,
        .peak_confidence = peak,
        .inference_ms = inference_ms,
    };
}

// ---- 1. Classical mode reproduces the classical detector exactly -------

void test_classical_mode_passes_through() {
    // Present.
    {
        const auto classical = classical_at(310.0, 220.0);
        const PerceptionResult r = resolve_perception(PerceptionMode::Classical, classical, std::nullopt);
        CHECK(r.detection.has_value());
        CHECK(r.detection->centroid_px.x_px == 310.0);
        CHECK(r.detection->centroid_px.y_px == 220.0);
        CHECK(r.diagnostics.perception_mode == PerceptionMode::Classical);
        CHECK(r.diagnostics.perception_source == PerceptionSource::Classical);
        CHECK(r.diagnostics.classical_detected);
        CHECK(!r.diagnostics.ai_candidate_detected);
        CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::NotApplicable);
    }
    // Absent -- an AI candidate present must NOT leak into Classical mode.
    {
        const PerceptionResult r =
            resolve_perception(PerceptionMode::Classical, std::nullopt, ai_at(400.0, 100.0));
        CHECK(!r.detection.has_value());
        CHECK(r.diagnostics.perception_source == PerceptionSource::None);
        CHECK(!r.diagnostics.classical_detected);
        CHECK(!r.diagnostics.ai_candidate_detected);
    }
}

// ---- 2. AI mode exposes the thresholded AI candidate directly ----------

void test_ai_mode_exposes_candidate() {
    {
        const auto ai = ai_at(150.5, 60.25, 0.99, 0.98, 2.2);
        const PerceptionResult r = resolve_perception(PerceptionMode::AI, std::nullopt, ai);
        CHECK(r.detection.has_value());
        CHECK(r.detection->centroid_px.x_px == 150.5);
        CHECK(r.detection->centroid_px.y_px == 60.25);
        CHECK(r.diagnostics.perception_source == PerceptionSource::AI);
        CHECK(r.diagnostics.ai_candidate_detected);
        CHECK(r.diagnostics.ai_presence_probability.has_value() && *r.diagnostics.ai_presence_probability == 0.99);
        CHECK(r.diagnostics.ai_inference_ms.has_value() && *r.diagnostics.ai_inference_ms == 2.2);
        CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::NotApplicable);
    }
    {
        const PerceptionResult r = resolve_perception(PerceptionMode::AI, std::nullopt, std::nullopt);
        CHECK(!r.detection.has_value());
        CHECK(r.diagnostics.perception_source == PerceptionSource::None);
        CHECK(!r.diagnostics.ai_candidate_detected);
    }
}

// ---- 3/4. Hybrid Case A: both agree -> accept CLASSICAL centroid -------

void test_hybrid_agreement_uses_classical_centroid() {
    const auto classical = classical_at(300.0, 200.0);
    const auto ai = ai_at(303.0, 204.0);  // distance = 5.0 px, well inside the radius
    const PerceptionResult r = resolve_perception(PerceptionMode::Hybrid, classical, ai);

    CHECK(r.detection.has_value());
    // Control-facing centroid is the CLASSICAL one, NOT the AI one.
    CHECK(r.detection->centroid_px.x_px == 300.0);
    CHECK(r.detection->centroid_px.y_px == 200.0);
    CHECK(r.diagnostics.perception_source == PerceptionSource::HybridAgreement);
    CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::NotApplicable);
    CHECK(r.diagnostics.classical_ai_distance_px.has_value());
    CHECK(*r.diagnostics.classical_ai_distance_px == 5.0);
}

// ---- 5. Hybrid Case B: classical only -> accept classical --------------

void test_hybrid_classical_only() {
    const PerceptionResult r =
        resolve_perception(PerceptionMode::Hybrid, classical_at(250.0, 175.0), std::nullopt);
    CHECK(r.detection.has_value());
    CHECK(r.detection->centroid_px.x_px == 250.0);
    CHECK(r.diagnostics.perception_source == PerceptionSource::Classical);
    CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::NotApplicable);
    CHECK(!r.diagnostics.classical_ai_distance_px.has_value());
}

// ---- 6/7. Hybrid Case C: AI only -> AiOnlyUnverified, NO control authority --

void test_hybrid_ai_only_unverified() {
    const PerceptionResult r =
        resolve_perception(PerceptionMode::Hybrid, std::nullopt, ai_at(400.0, 100.0, 0.999));
    CHECK(!r.detection.has_value());  // no control authority, even at very high confidence
    CHECK(r.diagnostics.perception_source == PerceptionSource::None);
    CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::AiOnlyUnverified);
    // Diagnostics still retain the AI candidate info even though it was rejected.
    CHECK(r.diagnostics.ai_candidate_detected);
    CHECK(r.diagnostics.ai_presence_probability.has_value() && *r.diagnostics.ai_presence_probability == 0.999);
    CHECK(!r.diagnostics.classical_detected);
}

// ---- 8/9. Hybrid Case D: strong disagreement -> unconditional reject ----

void test_hybrid_disagreement_rejected_unconditionally() {
    // Classical at frame centre; AI locked onto a wrong blob far away, at an
    // implausibly high confidence -- confidence must NOT override the reject.
    const auto classical = classical_at(320.0, 240.0);
    const auto ai = ai_at(950.0 /* far off, deliberately absurd */, 240.0, 0.999, 0.999);
    const PerceptionResult r = resolve_perception(PerceptionMode::Hybrid, classical, ai);

    CHECK(!r.detection.has_value());  // reject -- not classical, not AI, not an average
    CHECK(r.diagnostics.perception_source == PerceptionSource::None);
    CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::DetectorDisagreement);
    CHECK(r.diagnostics.classical_ai_distance_px.has_value());
    CHECK(*r.diagnostics.classical_ai_distance_px > fsoc::kAgreementRadiusPx);
}

// ---- 10. Hybrid Case E: neither detects -> nullopt, NotApplicable ------

void test_hybrid_neither_detects() {
    const PerceptionResult r = resolve_perception(PerceptionMode::Hybrid, std::nullopt, std::nullopt);
    CHECK(!r.detection.has_value());
    CHECK(r.diagnostics.perception_source == PerceptionSource::None);
    CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::NotApplicable);
    CHECK(!r.diagnostics.classical_detected);
    CHECK(!r.diagnostics.ai_candidate_detected);
}

// ---- 11. Agreement radius boundary: <= 8.0 agrees, > 8.0 disagrees ------

void test_agreement_radius_boundary() {
    CHECK(fsoc::kAgreementRadiusPx == 8.0);

    // Exactly 8.0 px apart (pure x offset) -> agreement (frozen '<=' semantics).
    {
        const auto classical = classical_at(300.0, 200.0);
        const auto ai = ai_at(308.0, 200.0);
        const PerceptionResult r = resolve_perception(PerceptionMode::Hybrid, classical, ai);
        CHECK(r.diagnostics.classical_ai_distance_px.has_value());
        CHECK(*r.diagnostics.classical_ai_distance_px == 8.0);
        CHECK(r.detection.has_value());
        CHECK(r.diagnostics.perception_source == PerceptionSource::HybridAgreement);
        CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::NotApplicable);
    }
    // Just over 8.0 px (8.0 + 1e-6) -> disagreement.
    {
        const auto classical = classical_at(300.0, 200.0);
        const auto ai = ai_at(308.000001, 200.0);
        const PerceptionResult r = resolve_perception(PerceptionMode::Hybrid, classical, ai);
        CHECK(*r.diagnostics.classical_ai_distance_px > 8.0);
        CHECK(!r.detection.has_value());
        CHECK(r.diagnostics.perception_source == PerceptionSource::None);
        CHECK(r.diagnostics.rejection_reason == PerceptionRejectionReason::DetectorDisagreement);
    }
    // Just under 8.0 px -> agreement.
    {
        const auto classical = classical_at(300.0, 200.0);
        const auto ai = ai_at(307.999999, 200.0);
        const PerceptionResult r = resolve_perception(PerceptionMode::Hybrid, classical, ai);
        CHECK(*r.diagnostics.classical_ai_distance_px < 8.0);
        CHECK(r.detection.has_value());
        CHECK(r.diagnostics.perception_source == PerceptionSource::HybridAgreement);
    }
}

// ---- 12. Diagnostics correctly identify source across every case -------

void test_diagnostics_source_matrix() {
    struct Case {
        PerceptionMode mode;
        std::optional<BeaconDetection> classical;
        std::optional<AiBeaconDetection> ai;
        PerceptionSource expected_source;
        PerceptionRejectionReason expected_reason;
    };
    const std::vector<Case> cases = {
        {PerceptionMode::Hybrid, classical_at(0, 0), ai_at(1, 1), PerceptionSource::HybridAgreement,
         PerceptionRejectionReason::NotApplicable},
        {PerceptionMode::Hybrid, classical_at(0, 0), std::nullopt, PerceptionSource::Classical,
         PerceptionRejectionReason::NotApplicable},
        {PerceptionMode::Hybrid, std::nullopt, ai_at(1, 1), PerceptionSource::None,
         PerceptionRejectionReason::AiOnlyUnverified},
        {PerceptionMode::Hybrid, classical_at(0, 0), ai_at(500, 500), PerceptionSource::None,
         PerceptionRejectionReason::DetectorDisagreement},
        {PerceptionMode::Hybrid, std::nullopt, std::nullopt, PerceptionSource::None,
         PerceptionRejectionReason::NotApplicable},
    };
    for (const Case& c : cases) {
        const PerceptionResult r = resolve_perception(c.mode, c.classical, c.ai);
        CHECK(r.diagnostics.perception_source == c.expected_source);
        CHECK(r.diagnostics.rejection_reason == c.expected_reason);
    }
}

}  // namespace

int main() {
    test_classical_mode_passes_through();
    test_ai_mode_exposes_candidate();
    test_hybrid_agreement_uses_classical_centroid();
    test_hybrid_classical_only();
    test_hybrid_ai_only_unverified();
    test_hybrid_disagreement_rejected_unconditionally();
    test_hybrid_neither_detects();
    test_agreement_radius_boundary();
    test_diagnostics_source_matrix();

    if (failures == 0) {
        std::cout << "PASS: Stage-3 Safe Hybrid (ADR-018) checks passed.\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " check(s).\n";
    return 1;
}
