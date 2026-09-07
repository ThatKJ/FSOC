#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace fsoc::stage4 {

// ---------------------------------------------------------------------------
// Stage-4 metrics (frozen definitions — docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md §7)
// ---------------------------------------------------------------------------

// Nearest-rank 95th percentile over `values` (sorted internally; not mutated
// in place since it takes by value). Same method already frozen for telemetry
// in docs/08_TELEMETRY_SCHEMA.md: index = ceil(0.95*N) - 1, clamped to [0,N-1].
// Empty input returns 0.0.
[[nodiscard]] double percentile95_nearest_rank(std::vector<double> values);

struct LocalizationStats {
    std::size_t n{0};
    double median_px{0.0};
    double mae_px{0.0};
    double rmse_px{0.0};
    double p95_px{0.0};
    double max_px{0.0};
};

// Cumulative (not exclusive) — a >50px error also counts toward gt_20.
struct OutlierCounts {
    std::size_t gt_20{0};
    std::size_t gt_50{0};
    std::size_t gt_100{0};
};

struct PerceptionRateStats {
    std::size_t total_frames{0};
    std::size_t positive_frames{0};   // truth: target present
    std::size_t negative_frames{0};   // truth: target absent
    std::size_t accepted_frames{0};   // mode produced a control-facing/candidate output
    std::size_t true_positive{0};     // accepted AND truth present
    std::size_t false_positive{0};    // accepted AND truth absent
    double accepted_rate{0.0};        // accepted_frames / total_frames
    double recall{0.0};               // true_positive / positive_frames  (a.k.a. "detection_rate")
    double precision{0.0};            // true_positive / accepted_frames
    double fpr{0.0};                  // false_positive / negative_frames
    LocalizationStats localization{}; // over true-positive accepted frames only
    OutlierCounts outliers{};
};

// Ingests one frame at a time; `error_px_if_true_positive` must be set iff
// `truth_present && accepted` and the mode's accepted output has a centroid
// to score (always true for this project's detector contracts).
class PerceptionRateAccumulator {
public:
    void add_frame(bool truth_present, bool accepted, std::optional<double> error_px_if_true_positive);
    [[nodiscard]] PerceptionRateStats finalize() const;

private:
    std::size_t total_{0};
    std::size_t positive_{0};
    std::size_t negative_{0};
    std::size_t accepted_{0};
    std::size_t true_positive_{0};
    std::size_t false_positive_{0};
    std::vector<double> true_positive_errors_px_;
};

}  // namespace fsoc::stage4
