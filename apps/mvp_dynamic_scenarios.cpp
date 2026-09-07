// MVP-V2 Phase G — dynamic evaluation scenarios.
//
// The frozen Stage-4 protocol (docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md) tests
// 11 scenarios of image DEGRADATION (noise/blur/clutter/absence) against a
// single StationaryTrajectory -- it never exercises target DYNAMICS (moving
// target, direction change, detection dropout, reacquisition). This is a NEW,
// separate, additive tool -- it does not touch stage4_scenarios.hpp,
// stage4_degradation.cpp, or any frozen Stage-4 file or number. It reuses the
// same frozen production primitives (PanTiltCamera, SyntheticCameraRenderer,
// BeaconDetector, AiBeaconDetector, resolve_perception, TargetTracker,
// PIDController) the real fsoc::SimulationRunner and the Stage-4 bench use --
// nothing here reimplements projection, detection, fusion, estimation, or
// control math.
//
// Scenarios NOT duplicated here because Stage-4 already covers them:
// stationary+noise (LowSnr), clutter distractors (StarClutter/BrightDistractor,
// see docs/MVP_ABLATION.md), blur (Blur/MotionBlur). "Reacquisition" is not a
// separate scenario -- it is the reacquisition-time metric measured on every
// dropout scenario below (mean_reacquisition_time_frames).
//
// Usage: mvp_dynamic_scenarios [--model PATH] [--out DIR] [--quick]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "fsoc/ai_beacon_detector.hpp"
#include "fsoc/camera.hpp"
#include "fsoc/config.hpp"
#include "fsoc/detector.hpp"
#include "fsoc/observation.hpp"
#include "fsoc/perception.hpp"
#include "fsoc/pid_controller.hpp"
#include "fsoc/renderer.hpp"
#include "fsoc/stage4_degradation.hpp"
#include "fsoc/target_state.hpp"
#include "fsoc/target_tracker.hpp"
#include "fsoc/tracking_error.hpp"
#include "fsoc/trajectory.hpp"

namespace fs = std::filesystem;
using fsoc::AiBeaconDetector;
using fsoc::AiBeaconDetectorConfig;
using fsoc::BeaconDetector;
using fsoc::BeaconDetectorConfig;
using fsoc::PerceptionMode;
using fsoc::TargetTracker;
using fsoc::TargetTrackerConfig;
using fsoc::Vec3;
namespace stage4 = fsoc::stage4;

namespace {

// ---------------------------------------------------------------------------
// New trajectories, local to this evaluation (not added to fsoc/trajectory.hpp
// -- these are evaluation-only stress shapes, not general-purpose primitives).
// ---------------------------------------------------------------------------

// Constant velocity, then an instantaneous velocity-vector switch at
// switch_time_s. Exercises the tracker's velocity estimate under a step
// change -- Phase C's "no indefinite extrapolation" claim is meaningless
// without a scenario that actually changes direction.
class SuddenDirectionChangeTrajectory final : public fsoc::Trajectory {
public:
    SuddenDirectionChangeTrajectory(
        const Vec3 initial_position_m, const Vec3 velocity_before_mps, const Vec3 velocity_after_mps,
        const double switch_time_s)
        : initial_position_m_(initial_position_m),
          velocity_before_mps_(velocity_before_mps),
          velocity_after_mps_(velocity_after_mps),
          switch_time_s_(switch_time_s) {}

    [[nodiscard]] fsoc::TargetState state_at(const double time_s) const override {
        require_valid_time_s(time_s);
        if (time_s <= switch_time_s_) {
            return fsoc::TargetState{.position_m = initial_position_m_ + velocity_before_mps_ * time_s,
                                      .velocity_mps = velocity_before_mps_};
        }
        const Vec3 position_at_switch = initial_position_m_ + velocity_before_mps_ * switch_time_s_;
        const double t_after = time_s - switch_time_s_;
        return fsoc::TargetState{.position_m = position_at_switch + velocity_after_mps_ * t_after,
                                  .velocity_mps = velocity_after_mps_};
    }

private:
    Vec3 initial_position_m_;
    Vec3 velocity_before_mps_;
    Vec3 velocity_after_mps_;
    double switch_time_s_;
};

// Stationary except for an exact, engineered [dropout_start_s, dropout_end_s)
// window during which the target is far outside the FOV -- an exact-length
// detection dropout / temporary-occlusion proxy (this simulator has no
// separate occluding-object renderer; moving the target out of the
// detectable region is the honest, available stand-in). Mirrors
// tests/step7_tests.cpp's BriefDropoutTrajectory exactly (duplicated here
// rather than shared -- both are evaluation-only, one C++-test-local, one
// app-local).
class BriefDropoutTrajectory final : public fsoc::Trajectory {
public:
    BriefDropoutTrajectory(
        const Vec3 in_fov_position_m, const Vec3 out_of_fov_position_m, const double dropout_start_s,
        const double dropout_end_s)
        : in_position_(in_fov_position_m),
          out_position_(out_of_fov_position_m),
          dropout_start_s_(dropout_start_s),
          dropout_end_s_(dropout_end_s) {}

    [[nodiscard]] fsoc::TargetState state_at(const double time_s) const override {
        require_valid_time_s(time_s);
        fsoc::TargetState state{};
        state.position_m =
            (time_s >= dropout_start_s_ && time_s < dropout_end_s_) ? out_position_ : in_position_;
        state.velocity_mps = Vec3{0.0, 0.0, 0.0};
        return state;
    }

private:
    Vec3 in_position_;
    Vec3 out_position_;
    double dropout_start_s_;
    double dropout_end_s_;
};

// A single Gaussian blob, composited directly onto an existing CV_8UC1 frame
// (additive, clamped to 255). Small, local reimplementation of the same
// primitive stage4_degradation.cpp's apply_clutter() uses internally (that
// one is anonymous-namespace, not exported) -- needed here because a MOVING
// distractor requires a caller-chosen position per frame, which
// stage4::apply_degradation() deliberately does not support (it only ever
// places clutter/distractors randomly, independent of any external state).
void composite_gaussian_blob(cv::Mat& frame_u8, const double cx, const double cy, const double peak,
                              const double sigma) {
    const double window = 4.0 * sigma;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - window)));
    const int x1 = std::min(frame_u8.cols - 1, static_cast<int>(std::ceil(cx + window)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - window)));
    const int y1 = std::min(frame_u8.rows - 1, static_cast<int>(std::ceil(cy + window)));
    const double inv_two_sigma_sq = 1.0 / (2.0 * sigma * sigma);
    for (int y = y0; y <= y1; ++y) {
        auto* row = frame_u8.ptr<std::uint8_t>(y);
        const double dy = static_cast<double>(y) - cy;
        for (int x = x0; x <= x1; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double g = peak * std::exp(-(dx * dx + dy * dy) * inv_two_sigma_sq);
            const double value = static_cast<double>(row[x]) + g;
            row[x] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
        }
    }
}

// ---------------------------------------------------------------------------
// Scenario definitions
// ---------------------------------------------------------------------------

enum class DynamicScenarioId {
    ConstantVelocity,
    SuddenDirectionChange,
    Dropout1Frame,
    Dropout2Frame,
    DropoutLonger,
    MovingDistractor,
    OverexposureGlareProxy,
    EdgeOfFrame,
};

struct DynamicScenario {
    DynamicScenarioId id;
    const char* label;
    double duration_s;
    // Exactly one of trajectory/*: constructed fresh per (scenario, seed) run
    // since some are stateful-by-value only (cheap to rebuild).
    std::function<std::unique_ptr<fsoc::Trajectory>()> make_trajectory;
    fsoc::stage4::DegradationConfig degradation;  // {} = clean
    bool moving_distractor;  // if true, composite_gaussian_blob is applied per-frame
    double initial_pan_rad;
    double initial_tilt_rad;
};

constexpr double kDt = 0.02;

[[nodiscard]] std::vector<DynamicScenario> make_scenarios() {
    std::vector<DynamicScenario> scenarios;

    scenarios.push_back({
        DynamicScenarioId::ConstantVelocity, "CONSTANT_VELOCITY", 6.0,
        [] { return std::make_unique<fsoc::LinearTrajectory>(Vec3{100.0, -12.0, 3.0}, Vec3{0.0, 4.0, 0.0}); },
        {}, false, 0.0, 0.0,
    });

    scenarios.push_back({
        DynamicScenarioId::SuddenDirectionChange, "SUDDEN_DIRECTION_CHANGE", 6.0,
        [] {
            return std::make_unique<SuddenDirectionChangeTrajectory>(
                Vec3{100.0, 0.0, 3.0}, Vec3{0.0, 6.0, 0.0}, Vec3{0.0, -6.0, 0.0}, 3.0);
        },
        {}, false, 0.0, 0.0,
    });

    // Dropout scenarios: target centered at t=0, leaves the FOV for exactly N
    // frames starting at frame 20 (0.40s), returns to the SAME position (so
    // any reacquisition delay is attributable to the tracker/perception
    // policy, not to the target having moved).
    scenarios.push_back({
        DynamicScenarioId::Dropout1Frame, "DROPOUT_1_FRAME", 2.0,
        [] {
            return std::make_unique<BriefDropoutTrajectory>(
                Vec3{100.0, 0.0, 0.0}, Vec3{100.0, 500.0, 0.0}, 20.0 * kDt, 21.0 * kDt);
        },
        {}, false, 0.0, 0.0,
    });
    scenarios.push_back({
        DynamicScenarioId::Dropout2Frame, "DROPOUT_2_FRAME", 2.0,
        [] {
            return std::make_unique<BriefDropoutTrajectory>(
                Vec3{100.0, 0.0, 0.0}, Vec3{100.0, 500.0, 0.0}, 20.0 * kDt, 22.0 * kDt);
        },
        {}, false, 0.0, 0.0,
    });
    scenarios.push_back({
        DynamicScenarioId::DropoutLonger, "DROPOUT_LONGER_10_FRAMES", 2.0,
        [] {
            return std::make_unique<BriefDropoutTrajectory>(
                Vec3{100.0, 0.0, 0.0}, Vec3{100.0, 500.0, 0.0}, 20.0 * kDt, 30.0 * kDt);
        },
        {}, false, 0.0, 0.0,
    });

    scenarios.push_back({
        DynamicScenarioId::MovingDistractor, "MOVING_DISTRACTOR", 4.0,
        [] { return std::make_unique<fsoc::StationaryTrajectory>(Vec3{100.0, 0.0, 0.0}); }, {}, true, 0.0, 0.0,
    });

    // Overexposure / veiling-glare PROXY: an extreme uniform background
    // gradient (not a physically modeled lens flare -- this simulator has no
    // optical flare model) pushes much of the frame toward saturation,
    // stressing "brightest region" selection the way real glare would.
    fsoc::stage4::DegradationConfig glare{};
    glare.gradient_amplitude = 230.0;
    scenarios.push_back({
        DynamicScenarioId::OverexposureGlareProxy, "OVEREXPOSURE_GLARE_PROXY", 4.0,
        [] { return std::make_unique<fsoc::StationaryTrajectory>(Vec3{100.0, 0.0, 0.0}); }, glare, false, 0.0,
        0.0,
    });

    // Edge-of-frame: target starts near the camera's angular field-of-view
    // boundary (camera pan=tilt=0, the SAME validated {100, 15.5, 8} m
    // position as Step-10's "Near-FOV-Edge Acquisition" -- docs/16, ~88% /
    // 60% of the half-FOV) evaluated here through the Classical/Hybrid/
    // tracker comparison Step-10 never ran.
    scenarios.push_back({
        DynamicScenarioId::EdgeOfFrame, "EDGE_OF_FRAME", 4.0,
        [] { return std::make_unique<fsoc::StationaryTrajectory>(Vec3{100.0, 15.5, 8.0}); }, {}, false, 0.0,
        0.0,
    });

    return scenarios;
}

struct RunConfig {
    const char* label;
    PerceptionMode mode;
    bool tracker_enabled;
};

constexpr std::array<RunConfig, 4> kRunConfigs = {{
    {"CLASSICAL", PerceptionMode::Classical, false},
    {"CLASSICAL+TRACKER", PerceptionMode::Classical, true},
    {"HYBRID", PerceptionMode::Hybrid, false},
    {"HYBRID+TRACKER(V2)", PerceptionMode::Hybrid, true},
}};

constexpr std::array<std::uint64_t, 5> kSeeds = {510101ULL, 510102ULL, 510103ULL, 510104ULL, 510105ULL};

struct SeedResult {
    std::size_t total_frames{0};
    std::size_t accepted_frames{0};
    std::vector<double> reacquisition_times_frames;
    std::vector<double> angular_errors_rad;
    std::size_t control_outlier_gt20{0};
    std::size_t control_outlier_gt50{0};
    std::size_t control_outlier_gt100{0};
    double max_control_error_px{0.0};
};

struct FinalResult {
    double accepted_fraction{0.0};
    std::size_t control_outlier_gt20{0};
    std::size_t control_outlier_gt50{0};
    std::size_t control_outlier_gt100{0};
    double max_control_error_px{0.0};
    double rms_angular_error_deg{0.0};
    std::size_t reacquisition_count{0};
    double mean_reacquisition_time_frames{0.0};
};

[[nodiscard]] FinalResult finalize(const std::vector<SeedResult>& per_seed) {
    FinalResult out{};
    std::size_t total = 0;
    std::size_t accepted = 0;
    std::vector<double> pooled_angular;
    std::vector<double> pooled_reacq;
    for (const SeedResult& r : per_seed) {
        total += r.total_frames;
        accepted += r.accepted_frames;
        out.control_outlier_gt20 += r.control_outlier_gt20;
        out.control_outlier_gt50 += r.control_outlier_gt50;
        out.control_outlier_gt100 += r.control_outlier_gt100;
        out.max_control_error_px = std::max(out.max_control_error_px, r.max_control_error_px);
        pooled_angular.insert(pooled_angular.end(), r.angular_errors_rad.begin(), r.angular_errors_rad.end());
        pooled_reacq.insert(pooled_reacq.end(), r.reacquisition_times_frames.begin(),
                             r.reacquisition_times_frames.end());
    }
    out.accepted_fraction = total > 0 ? static_cast<double>(accepted) / static_cast<double>(total) : 0.0;
    if (!pooled_angular.empty()) {
        double sum_sq = 0.0;
        for (const double a : pooled_angular) sum_sq += a * a;
        out.rms_angular_error_deg =
            std::sqrt(sum_sq / static_cast<double>(pooled_angular.size())) * 180.0 / std::numbers::pi_v<double>;
    }
    out.reacquisition_count = pooled_reacq.size();
    out.mean_reacquisition_time_frames =
        pooled_reacq.empty() ? 0.0
                             : std::accumulate(pooled_reacq.begin(), pooled_reacq.end(), 0.0) /
                                   static_cast<double>(pooled_reacq.size());
    return out;
}

[[nodiscard]] SeedResult run_one(
    const DynamicScenario& scenario, const RunConfig& cfg, const std::uint64_t seed,
    const BeaconDetector& classical_detector, const AiBeaconDetector& ai_detector) {
    const fsoc::CameraConfig camera_config{};
    fsoc::PanTiltCamera camera{camera_config, Vec3{0.0, 0.0, 0.0}, scenario.initial_pan_rad,
                               scenario.initial_tilt_rad};
    const fsoc::SyntheticCameraRenderer renderer{fsoc::renderer_config_for(camera_config, 2.0)};
    const std::unique_ptr<fsoc::Trajectory> trajectory = scenario.make_trajectory();

    fsoc::PIDAxisConfig axis{};
    axis.kp = 12.0;
    axis.ki = 0.0;
    axis.kd = 0.0;
    axis.integral_limit = 0.0;
    axis.output_limit_rad_s = camera_config.max_pan_rate_rad_s;
    fsoc::PIDControllerConfig pid_config{};
    pid_config.pan = axis;
    pid_config.tilt = axis;
    fsoc::PIDController pid{pid_config};

    std::optional<TargetTracker> tracker;
    if (cfg.tracker_enabled) {
        tracker.emplace(TargetTrackerConfig{});
    }

    const auto step_count = static_cast<std::size_t>(std::ceil(scenario.duration_s / kDt));
    SeedResult raw{};
    raw.total_frames = step_count;

    std::size_t current_loss_streak = 0;
    bool was_lost_before = false;
    double sim_time_s = 0.0;

    for (std::size_t frame_index = 0; frame_index < step_count; ++frame_index) {
        const fsoc::TargetState target = trajectory->state_at(sim_time_s);
        const fsoc::CameraObservation observation = fsoc::observe_beacon(camera, target.position_m);
        cv::Mat frame = renderer.render(observation);
        if (scenario.moving_distractor) {
            // Sweeps left-to-right across the whole run, independent of the
            // beacon -- a persistent, spatially COHERENT (not per-frame
            // re-randomized) distractor, unlike Stage-4's BrightDistractor.
            const double t_frac = sim_time_s / scenario.duration_s;
            const double dx = t_frac * static_cast<double>(frame.cols - 1);
            // sigma=3.0 at peak=255 gives integrated signal ~255*3.0^2 = 2295,
            // versus the beacon's own ~255*2.0^2 = 1020 (RendererConfig
            // defaults) -- deliberately brighter/larger than the beacon by
            // total signal, so Classical's "greatest integrated signal" rule
            // genuinely has to choose between them, not ignore a token blob.
            composite_gaussian_blob(frame, dx, static_cast<double>(frame.rows) * 0.3, 255.0, 3.0);
        }
        if (scenario.degradation.gradient_amplitude > 0.0 || scenario.degradation.star_count > 0 ||
            scenario.degradation.read_sigma > 0.0) {
            frame = stage4::apply_degradation(frame, seed + frame_index * 2654435761ULL, scenario.degradation);
        }

        std::optional<fsoc::BeaconDetection> classical_detection{};
        std::optional<fsoc::AiBeaconDetection> ai_detection{};
        if (cfg.mode != PerceptionMode::AI) {
            classical_detection = classical_detector.detect(frame);
        }
        if (cfg.mode != PerceptionMode::Classical) {
            ai_detection = ai_detector.detect(frame);
        }
        const fsoc::PerceptionResult perception = fsoc::resolve_perception(cfg.mode, classical_detection, ai_detection);

        std::optional<fsoc::BeaconDetection> final_detection = perception.detection;
        if (tracker.has_value()) {
            const std::optional<fsoc::ImagePoint> measurement =
                perception.detection.has_value()
                    ? std::optional<fsoc::ImagePoint>(perception.detection->centroid_px)
                    : std::nullopt;
            const fsoc::TrackedState tracked = tracker->update(measurement, kDt);
            final_detection = fsoc::is_safe_to_steer(tracked, 0.4)
                                   ? std::optional<fsoc::BeaconDetection>(
                                         fsoc::BeaconDetection{.centroid_px = {tracked.x_px, tracked.y_px}})
                                   : std::nullopt;
        }

        if (final_detection.has_value()) {
            ++raw.accepted_frames;
        }
        if (final_detection.has_value() && observation.image_point_px.has_value()) {
            const double dx = final_detection->centroid_px.x_px - observation.image_point_px->x_px;
            const double dy = final_detection->centroid_px.y_px - observation.image_point_px->y_px;
            const double error_px = std::hypot(dx, dy);
            raw.max_control_error_px = std::max(raw.max_control_error_px, error_px);
            if (error_px > 20.0) ++raw.control_outlier_gt20;
            if (error_px > 50.0) ++raw.control_outlier_gt50;
            if (error_px > 100.0) ++raw.control_outlier_gt100;
        }

        const std::optional<fsoc::TrackingError> tracking_error = fsoc::compute_tracking_error(final_detection, camera);
        const bool tracking_now = tracking_error.has_value();
        if (!tracking_now) {
            ++current_loss_streak;
            was_lost_before = true;
        } else {
            if (was_lost_before && current_loss_streak > 0) {
                raw.reacquisition_times_frames.push_back(static_cast<double>(current_loss_streak));
            }
            current_loss_streak = 0;
            was_lost_before = false;
            raw.angular_errors_rad.push_back(
                std::hypot(tracking_error->angular.pan_rad, tracking_error->angular.tilt_rad));
        }

        fsoc::ControlCommand command = fsoc::zero_control_command();
        if (tracking_error.has_value()) {
            command = pid.update(*tracking_error, kDt);
        } else {
            pid.reset();
        }
        (void)camera.step(command.pan_rate_rad_s, command.tilt_rate_rad_s, kDt);
        sim_time_s += kDt;
    }

    return raw;
}

}  // namespace

int main(int argc, char** argv) {
    std::string model_path = "models/tiny_beacon_net.onnx";
    std::string out_dir = "generated/mvp_dynamic_scenarios";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }
    fs::create_directories(out_dir + "/csv");

    AiBeaconDetectorConfig ai_config{};
    ai_config.model_path = model_path;
    ai_config.presence_threshold = 0.95;
    std::unique_ptr<AiBeaconDetector> ai_detector;
    try {
        ai_detector = std::make_unique<AiBeaconDetector>(ai_config);
    } catch (const std::exception& e) {
        std::cerr << "mvp_dynamic_scenarios: failed to load model '" << model_path << "': " << e.what() << '\n';
        return 1;
    }
    const BeaconDetector classical_detector{BeaconDetectorConfig{}};

    const auto scenarios = make_scenarios();
    std::ofstream csv(out_dir + "/csv/dynamic_scenarios.csv");
    csv << "scenario,config,total_frames,accepted_fraction,control_outlier_gt20,control_outlier_gt50,"
           "control_outlier_gt100,max_control_error_px,rms_angular_error_deg,reacquisition_count,"
           "mean_reacquisition_time_frames\n";

    std::cout << "\n=== MVP-V2 PHASE G: DYNAMIC SCENARIOS -- " << kSeeds.size() << " seeds ===\n";

    for (const auto& scenario : scenarios) {
        std::cout << "[" << scenario.label << "] duration=" << scenario.duration_s << "s\n";
        for (const auto& cfg : kRunConfigs) {
            std::vector<SeedResult> per_seed;
            std::size_t total_frames = 0;
            for (const std::uint64_t seed : kSeeds) {
                const auto r = run_one(scenario, cfg, seed, classical_detector, *ai_detector);
                total_frames = r.total_frames;
                per_seed.push_back(r);
            }
            const FinalResult f = finalize(per_seed);
            csv << scenario.label << ',' << cfg.label << ',' << total_frames << ',' << f.accepted_fraction << ','
                << f.control_outlier_gt20 << ',' << f.control_outlier_gt50 << ',' << f.control_outlier_gt100 << ','
                << f.max_control_error_px << ',' << f.rms_angular_error_deg << ',' << f.reacquisition_count << ','
                << f.mean_reacquisition_time_frames << '\n';
            std::cout << std::fixed << std::setprecision(4) << "  " << std::setw(20) << std::left << cfg.label
                      << std::right << " accepted=" << f.accepted_fraction << " ctrl_outlier(>20/50/100)="
                      << f.control_outlier_gt20 << "/" << f.control_outlier_gt50 << "/" << f.control_outlier_gt100
                      << " max_err_px=" << f.max_control_error_px << " RMS_deg=" << f.rms_angular_error_deg
                      << " reacq_n=" << f.reacquisition_count
                      << " reacq_mean_frames=" << f.mean_reacquisition_time_frames << "\n";
        }
    }
    csv.close();
    std::cout << "\nWrote " << out_dir << "/csv/dynamic_scenarios.csv\n";
    return 0;
}
