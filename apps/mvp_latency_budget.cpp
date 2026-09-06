// MVP-V2 Phase L — full latency budget.
//
// Measures, on THIS development machine (never claimed as embedded/hardware
// performance), the wall-clock cost of every stage in the closed loop, in
// isolation and end-to-end, against the 20 ms / 50 Hz simulation budget:
//   perception (classical / AI / hybrid fusion) -- reuses the same
//     BeaconDetector / AiBeaconDetector / resolve_perception() the real
//     runner uses, on a real rendered frame (not a synthetic buffer);
//   estimation/prediction (TargetTracker::update) -- isolated;
//   controller (PIDController::update) -- isolated;
//   full step (SimulationRunner::step(), all four Classical/Tracker/Hybrid/
//     Hybrid+Tracker configs) -- end-to-end, the number that actually has to
//     fit inside dt = 0.02 s.
//
// This is evaluation-only: it drives the existing, frozen production code
// (SimulationRunner, TargetTracker, PIDController, BeaconDetector,
// AiBeaconDetector) and adds no new algorithm.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <vector>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/camera.hpp"
#include "fsoc/config.hpp"
#include "fsoc/demo.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/observation.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/pid_controller.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/target_tracker.hpp"
#include "fsoc/tracking_error.hpp"
#include "fsoc/trajectory.hpp"

using namespace fsoc;

namespace {

struct LatencyStats {
    double mean_ms{};
    double p95_ms{};
    double max_ms{};
};

[[nodiscard]] LatencyStats summarize(std::vector<double> samples_ms) {
    LatencyStats out{};
    if (samples_ms.empty()) return out;
    std::sort(samples_ms.begin(), samples_ms.end());
    out.mean_ms = std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0) / static_cast<double>(samples_ms.size());
    const auto rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(samples_ms.size())));
    out.p95_ms = samples_ms[std::min(rank > 0 ? rank - 1 : 0, samples_ms.size() - 1)];
    out.max_ms = samples_ms.back();
    return out;
}

void print_row(const char* label, const LatencyStats& s, const double budget_ms) {
    std::cout << std::left << std::setw(28) << label << std::right << std::fixed << std::setprecision(4)
              << "mean=" << std::setw(9) << s.mean_ms << " ms  P95=" << std::setw(9) << s.p95_ms
              << " ms  max=" << std::setw(9) << s.max_ms << " ms  (" << std::setprecision(1)
              << (100.0 * s.p95_ms / budget_ms) << "% of " << budget_ms << "ms budget)\n";
}

}  // namespace

int main() {
    constexpr double kBudgetMs = 20.0;  // 50 Hz
    constexpr int kIterations = 2000;

    std::cout << "=== MVP-V2 PHASE L: LATENCY BUDGET (development machine, NOT embedded hardware) ===\n";
    std::cout << kIterations << " iterations per measurement after 50-iteration warmup.\n\n";

    const CameraConfig camera_config{};
    PanTiltCamera camera{camera_config, Vec3{0.0, 0.0, 0.0}, 0.0, 0.0};
    const SyntheticCameraRenderer renderer{renderer_config_for(camera_config, 2.0)};
    const StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};
    const CameraObservation observation = observe_beacon(camera, target.state_at(0.0).position_m);
    const cv::Mat frame = renderer.render(observation);

    const BeaconDetector classical{BeaconDetectorConfig{}};
    AiBeaconDetectorConfig ai_cfg{};
    ai_cfg.model_path = "models/tiny_beacon_net.onnx";
    ai_cfg.presence_threshold = 0.95;
    std::optional<AiBeaconDetector> ai;
    bool ai_available = true;
    try {
        ai.emplace(ai_cfg);
    } catch (const std::exception& e) {
        ai_available = false;
        std::cout << "(AI detector unavailable: " << e.what() << " -- AI/Hybrid rows skipped)\n\n";
    }

    // ---- 1. Classical perception, isolated ----
    {
        std::vector<double> samples;
        for (int i = 0; i < 50; ++i) (void)classical.detect(frame);
        for (int i = 0; i < kIterations; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            (void)classical.detect(frame);
            const auto t1 = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        print_row("perception (classical)", summarize(samples), kBudgetMs);
    }

    // ---- 2. AI perception, isolated ----
    if (ai_available) {
        std::vector<double> samples;
        for (int i = 0; i < 50; ++i) (void)ai->detect(frame);
        for (int i = 0; i < kIterations; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            (void)ai->detect(frame);
            const auto t1 = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        print_row("perception (AI)", summarize(samples), kBudgetMs);
    }

    // ---- 3. Hybrid: classical + AI + resolve_perception, isolated ----
    if (ai_available) {
        std::vector<double> samples;
        for (int i = 0; i < 50; ++i) {
            const auto c = classical.detect(frame);
            const auto a = ai->detect(frame);
            (void)resolve_perception(PerceptionMode::Hybrid, c, a);
        }
        for (int i = 0; i < kIterations; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto c = classical.detect(frame);
            const auto a = ai->detect(frame);
            const auto r = resolve_perception(PerceptionMode::Hybrid, c, a);
            (void)r;
            const auto t1 = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        print_row("perception (hybrid fusion)", summarize(samples), kBudgetMs);
    }

    // ---- 4. Estimation/prediction: TargetTracker::update, isolated ----
    {
        std::vector<double> samples;
        TargetTracker tracker{TargetTrackerConfig{}};
        const ImagePoint measurement{320.0, 240.0};
        for (int i = 0; i < 50; ++i) (void)tracker.update(measurement, 0.02);
        for (int i = 0; i < kIterations; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            (void)tracker.update(measurement, 0.02);
            const auto t1 = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        print_row("estimation (TargetTracker)", summarize(samples), kBudgetMs);
    }

    // ---- 5. Controller: PIDController::update, isolated ----
    {
        std::vector<double> samples;
        PIDAxisConfig axis{};
        axis.kp = 12.0;
        axis.output_limit_rad_s = camera_config.max_pan_rate_rad_s;
        PIDControllerConfig pid_cfg{};
        pid_cfg.pan = axis;
        pid_cfg.tilt = axis;
        PIDController pid{pid_cfg};
        TrackingError error{};
        error.angular.pan_rad = 0.05;
        error.angular.tilt_rad = 0.02;
        for (int i = 0; i < 50; ++i) (void)pid.update(error, 0.02);
        for (int i = 0; i < kIterations; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            (void)pid.update(error, 0.02);
            const auto t1 = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        print_row("controller (PID)", summarize(samples), kBudgetMs);
    }

    std::cout << "\n--- full closed-loop step (SimulationRunner::step(), end-to-end) ---\n";

    // ---- 6. Full step, each config ----
    struct Cfg {
        const char* label;
        PerceptionMode mode;
        bool tracker_enabled;
    };
    const std::vector<Cfg> configs = {
        {"CLASSICAL", PerceptionMode::Classical, false},
        {"CLASSICAL+TRACKER", PerceptionMode::Classical, true},
        {"HYBRID", PerceptionMode::Hybrid, false},
        {"HYBRID+TRACKER (V2)", PerceptionMode::Hybrid, true},
    };
    for (const Cfg& cfg : configs) {
        if (cfg.mode != PerceptionMode::Classical && !ai_available) continue;
        SimulationRunnerConfig runner_cfg = baseline_runner_config();
        runner_cfg.perception_mode = cfg.mode;
        if (cfg.mode != PerceptionMode::Classical) {
            runner_cfg.ai_detector = ai_cfg;
        }
        runner_cfg.tracker_enabled = cfg.tracker_enabled;
        const StationaryTrajectory step_target{Vec3{100.0, 6.0, 4.0}};
        SimulationRunner runner{runner_cfg, step_target};
        for (int i = 0; i < 50; ++i) (void)runner.step();

        std::vector<double> samples;
        for (int i = 0; i < kIterations; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            (void)runner.step();
            const auto t1 = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        print_row(cfg.label, summarize(samples), kBudgetMs);
    }

    std::cout << "\nAll measurements: development-machine CPU wall-clock only. No claim of\n"
                 "embedded/hardware real-time performance is made or implied.\n";
    return 0;
}
