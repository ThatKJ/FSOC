#include "fsoc/stage4_metrics.hpp"

#include <algorithm>
#include <cmath>

namespace fsoc::stage4 {

double percentile95_nearest_rank(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    std::size_t index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
    index = (index == 0) ? 0 : index - 1;
    index = std::min(index, n - 1);
    return values[index];
}

void PerceptionRateAccumulator::add_frame(
    const bool truth_present, const bool accepted, const std::optional<double> error_px_if_true_positive) {
    ++total_;
    if (truth_present) {
        ++positive_;
    } else {
        ++negative_;
    }
    if (accepted) {
        ++accepted_;
        if (truth_present) {
            ++true_positive_;
            if (error_px_if_true_positive.has_value()) {
                true_positive_errors_px_.push_back(*error_px_if_true_positive);
            }
        } else {
            ++false_positive_;
        }
    }
}

PerceptionRateStats PerceptionRateAccumulator::finalize() const {
    PerceptionRateStats stats{};
    stats.total_frames = total_;
    stats.positive_frames = positive_;
    stats.negative_frames = negative_;
    stats.accepted_frames = accepted_;
    stats.true_positive = true_positive_;
    stats.false_positive = false_positive_;

    stats.accepted_rate = total_ > 0 ? static_cast<double>(accepted_) / static_cast<double>(total_) : 0.0;
    stats.recall = positive_ > 0 ? static_cast<double>(true_positive_) / static_cast<double>(positive_) : 0.0;
    stats.precision =
        accepted_ > 0 ? static_cast<double>(true_positive_) / static_cast<double>(accepted_) : 0.0;
    stats.fpr = negative_ > 0 ? static_cast<double>(false_positive_) / static_cast<double>(negative_) : 0.0;

    if (!true_positive_errors_px_.empty()) {
        std::vector<double> sorted = true_positive_errors_px_;
        std::sort(sorted.begin(), sorted.end());
        const auto n = sorted.size();

        double sum = 0.0;
        double sum_sq = 0.0;
        for (const double e : sorted) {
            sum += e;
            sum_sq += e * e;
            if (e > 20.0) ++stats.outliers.gt_20;
            if (e > 50.0) ++stats.outliers.gt_50;
            if (e > 100.0) ++stats.outliers.gt_100;
        }

        stats.localization.n = n;
        stats.localization.mae_px = sum / static_cast<double>(n);
        stats.localization.rmse_px = std::sqrt(sum_sq / static_cast<double>(n));
        stats.localization.max_px = sorted.back();
        stats.localization.median_px =
            (n % 2 == 1) ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
        stats.localization.p95_px = percentile95_nearest_rank(sorted);
    }

    return stats;
}

}  // namespace fsoc::stage4
