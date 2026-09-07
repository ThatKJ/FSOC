// Step 9 smoke test: engineering camera-view visualization.
//
// Headless (no cv::imshow / cv::waitKey). Runs Step-7 scenarios, reads the
// exact perception frame SimulationStepResult::rendered_frame already carries
// (the frame the detector actually consumed — not a re-render), annotates a
// COPY, and writes selected PNGs (+ optional MP4) into generated/step9/.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "fsoc/config.hpp"
#include "fsoc/geometry.hpp"
#include "fsoc/simulation_runner.hpp"
#include "fsoc/telemetry.hpp"
#include "fsoc/trajectory.hpp"
#include "fsoc/visualization.hpp"

namespace {

using fsoc::rad_to_deg;

[[nodiscard]] std::string idx4(const std::size_t i) {
    std::ostringstream os;
    os << std::setw(4) << std::setfill('0') << i;
    return os.str();
}

// Annotate a set of frames, write them as PNGs, return the annotated BGR
// frames (return value may be ignored).
std::vector<cv::Mat> annotate_and_write(
    const fsoc::TrackingVisualizer& visualizer,
    const fsoc::RecordedRun& run,
    const std::vector<std::size_t>& frame_indices,
    const std::string& out_prefix,
    double& total_annotate_us,
    std::size_t& png_count) {
    std::vector<cv::Mat> annotated;
    for (const std::size_t i : frame_indices) {
        if (i >= run.step_results.size()) {
            continue;
        }
        const fsoc::SimulationStepResult& result = run.step_results[i];
        const fsoc::TelemetryRecord& telemetry = run.telemetry[i];

        // The exact frame the detector ran on — stored on the result itself,
        // not re-rendered from `result.observation`.
        const cv::Mat& base_frame = result.rendered_frame;

        const auto t0 = std::chrono::steady_clock::now();
        const cv::Mat display = visualizer.annotate(base_frame, result, telemetry);
        const auto t1 = std::chrono::steady_clock::now();
        total_annotate_us += std::chrono::duration<double, std::micro>(t1 - t0).count();

        const std::string path = out_prefix + idx4(i) + ".png";
        if (fsoc::write_png(path, display)) {
            ++png_count;
        }
        annotated.push_back(display);
    }
    return annotated;
}

void print_frame_line(const fsoc::RecordedRun& run, const std::size_t i) {
    if (i >= run.telemetry.size()) {
        return;
    }
    const fsoc::TelemetryRecord& t = run.telemetry[i];
    std::cout << "    frame " << std::setw(4) << i << "  t=" << std::fixed << std::setprecision(2)
              << t.simulation_time_s << "s  state="
              << (t.tracking_state == fsoc::TrackingState::Tracking ? "TRACKING   " : "TARGET LOST");
    if (t.angular_error_total_rad.has_value()) {
        std::cout << " ang_err=" << std::setprecision(4)
                  << rad_to_deg(*t.angular_error_total_rad) << " deg";
        std::cout << " cmd_pan=" << std::setprecision(2)
                  << rad_to_deg(t.command_pan_rate_rad_s) << " deg/s";
    } else {
        std::cout << " ang_err=--            cmd_pan=" << std::setprecision(2)
                  << rad_to_deg(t.command_pan_rate_rad_s) << " deg/s";
    }
    std::cout << '\n';
}

}  // namespace

int main() {
    using namespace fsoc;

    std::filesystem::create_directories("generated/step9");
    const SimulationRunnerConfig base = baseline_runner_config();
    const TrackingVisualizer visualizer{VisualizationConfig{}};  // defaults: truth marker OFF

    std::cout << "Step 9: engineering camera-view visualization (headless)\n"
              << "output CV_8UC3 BGR; perception frame (CV_8UC1) never modified\n";

    bool ok = true;
    double annotate_us = 0.0;
    std::size_t png_total = 0;
    std::vector<cv::Mat> video_static;
    std::vector<cv::Mat> video_sinusoidal;

    // ================= SCENARIO A — STATIC ACQUISITION =================
    std::cout << "\n=== SCENARIO A: static acquisition ===\n";
    {
        const StationaryTrajectory target{Vec3{100.0, 6.0, 4.0}};
        SimulationRunner runner{base, target};
        const RecordedRun run = run_and_record(runner, 1.5);

        // t = 0.00, 0.10, 0.24, 0.50, 1.00, and the final frame.
        const std::vector<std::size_t> keyframes = {0, 5, 12, 25, 50,
                                                    run.step_results.size() - 1};
        const std::size_t before = png_total;
        annotate_and_write(visualizer, run, keyframes, "generated/step9/static_",
                           annotate_us, png_total);
        // A dense sequence (every frame) for the optional video.
        for (std::size_t i = 0; i < run.step_results.size(); ++i) {
            video_static.push_back(visualizer.annotate(
                run.step_results[i].rendered_frame, run.step_results[i], run.telemetry[i]));
        }

        for (const std::size_t i : keyframes) {
            print_frame_line(run, i);
        }
        const std::size_t last = run.step_results.size() - 1;
        const double first_err =
            rad_to_deg(run.telemetry.front().angular_error_total_rad.value_or(0.0));
        const double final_err =
            rad_to_deg(run.telemetry[last].angular_error_total_rad.value_or(999.0));
        const double first_cmd = rad_to_deg(run.telemetry.front().command_pan_rate_rad_s);
        std::cout << "  wrote " << (png_total - before) << " PNGs to generated/step9/static_*.png\n"
                  << "  story: first angular error " << std::setprecision(3) << first_err
                  << " deg (cmd pan " << std::setprecision(1) << first_cmd
                  << " deg/s)  ->  final angular error " << std::setprecision(4) << final_err
                  << " deg\n";
        ok = ok && first_err > 3.0 && first_cmd > 20.0 && final_err < 0.05;
    }

    // ================= SCENARIO B — SINUSOIDAL TRACKING =================
    std::cout << "\n=== SCENARIO B: sinusoidal tracking ===\n";
    {
        SinusoidalTrajectory::Parameters p{};
        p.center_position_m = Vec3{100.0, 0.0, 3.0};
        p.amplitude_m = Vec3{0.0, 22.0, 4.0};
        p.frequency_hz = Vec3{0.0, 0.12, 0.09};
        const SinusoidalTrajectory target{p};

        SimulationRunnerConfig cfg = base;
        cfg.initial_tilt_rad = std::atan2(3.0, 100.0);
        SimulationRunner runner{cfg, target};
        const RecordedRun run = run_and_record(runner, 10.0);

        std::vector<std::size_t> keyframes;
        for (std::size_t i = 0; i < run.step_results.size(); i += 25) {  // every 0.5 s
            keyframes.push_back(i);
        }
        std::size_t before = png_total;
        annotate_and_write(visualizer, run, keyframes, "generated/step9/sinusoidal_",
                           annotate_us, png_total);
        // dense frames for the optional video
        for (std::size_t i = 0; i < run.step_results.size(); ++i) {
            video_sinusoidal.push_back(visualizer.annotate(
                run.step_results[i].rendered_frame, run.step_results[i], run.telemetry[i]));
        }

        const auto m = evaluate(run.step_results);
        std::cout << "  wrote " << (png_total - before)
                  << " PNGs to generated/step9/sinusoidal_*.png\n"
                  << "  " << m.detected_frames << "/" << m.frame_count << " detected, RMS "
                  << std::setprecision(4) << rad_to_deg(m.rms_angular_error_rad)
                  << " deg, max " << rad_to_deg(m.max_angular_error_rad) << " deg\n";
        ok = ok && m.detection_fraction >= 0.95;
    }

    // ================= SCENARIO C — TARGET LOST =================
    std::cout << "\n=== SCENARIO C: target lost / reacquired ===\n";
    {
        SinusoidalTrajectory::Parameters p{};
        p.center_position_m = Vec3{100.0, 0.0, 3.0};
        p.amplitude_m = Vec3{0.0, 42.0, 4.0};   // sweeps well past the FOV edge
        p.frequency_hz = Vec3{0.0, 0.30, 0.10};
        const SinusoidalTrajectory target{p};

        SimulationRunnerConfig cfg = base;
        cfg.initial_tilt_rad = std::atan2(3.0, 100.0);
        SimulationRunner runner{cfg, target};
        const RecordedRun run = run_and_record(runner, 8.0);

        std::size_t lost_i = run.step_results.size();
        for (std::size_t i = 0; i < run.step_results.size(); ++i) {
            if (!run.step_results[i].detection.has_value()) {
                lost_i = i;
                break;
            }
        }
        std::size_t reacq_i = run.step_results.size();
        for (std::size_t i = lost_i + 1; i < run.step_results.size(); ++i) {
            if (run.step_results[i].detection.has_value()) {
                reacq_i = i;
                break;
            }
        }

        const bool have_lost = lost_i < run.step_results.size();
        const bool have_reacq = reacq_i < run.step_results.size();
        if (have_lost) {
            const cv::Mat f =
                visualizer.annotate(run.step_results[lost_i].rendered_frame,
                                    run.step_results[lost_i], run.telemetry[lost_i]);
            if (write_png("generated/step9/lost_lost.png", f)) {
                ++png_total;
            }
            print_frame_line(run, lost_i);
        }
        if (have_reacq) {
            const cv::Mat f =
                visualizer.annotate(run.step_results[reacq_i].rendered_frame,
                                    run.step_results[reacq_i], run.telemetry[reacq_i]);
            if (write_png("generated/step9/lost_reacquired.png", f)) {
                ++png_total;
            }
            print_frame_line(run, reacq_i);
        }
        std::cout << "  wrote generated/step9/lost_lost.png (frame " << lost_i
                  << ") + lost_reacquired.png (frame " << reacq_i << ")\n";
        // the lost frame must carry no detection at all.
        ok = ok && have_lost && !run.step_results[lost_i].detection.has_value() &&
             !run.telemetry[lost_i].detected_x_px.has_value() && have_reacq;
    }

    // ================= optional MP4 =================
    std::cout << "\n=== optional MP4 export ===\n";
    const bool mp4_static =
        try_write_mp4("generated/step9_static.mp4", video_static, 1.0 / base.timestep_s);
    const bool mp4_sin =
        try_write_mp4("generated/step9_sinusoidal.mp4", video_sinusoidal, 1.0 / base.timestep_s);
    std::cout << "  generated/step9_static.mp4      : " << (mp4_static ? "written" : "skipped")
              << '\n'
              << "  generated/step9_sinusoidal.mp4  : " << (mp4_sin ? "written" : "skipped")
              << "  (MP4 is optional; PNG sequence is the portable path)\n";

    std::cout << "\n=== summary ===\n"
              << "  PNGs written        : " << png_total << '\n'
              << "  mean annotate time  : " << std::setprecision(1)
              << annotate_us / static_cast<double>(std::max<std::size_t>(png_total, 1))
              << " us/frame (informational)\n";

    std::cout << "\nStep 9 smoke: " << (ok && png_total > 0 ? "PASS" : "FAIL") << '\n';
    return (ok && png_total > 0) ? 0 : 1;
}
