# Telemetry Schema (Step 8)

`fsoc::TelemetryRecord` is one flat, explicitly-named record per simulation frame,
produced by `make_telemetry_record(const SimulationStepResult&, max_pan_rate_rad_s,
max_tilt_rate_rad_s)`. Telemetry is an **observer**: it consumes `SimulationStepResult`
values and never calls back into the runner / PID / camera / detector / renderer /
trajectory. Running a simulation with or without telemetry yields a bit-identical
`SimulationStepResult` sequence.

## Absent-value policy

* **In memory:** optional fields are `std::optional<double>` and hold `std::nullopt` when
  the measurement is unavailable. There is **no** in-memory sentinel (`-1`, NaN, `N/A`).
* **In CSV:** an absent optional is written as an **empty field** between commas
  (`...,0,,,...`). Booleans are `0` / `1`. `tracking_state` is the string `Tracking` or
  `TargetLost`. Doubles are written with `setprecision(10)`.

## Fields

| # | name | type | unit | meaning | availability |
|--:|------|------|------|---------|--------------|
| 1 | `simulation_time_s` | double | s | fixed-step simulation clock for this frame (`frame_index * dt`) | always |
| 2 | `frame_index` | size_t | — | 0-based frame counter | always |
| 3 | `target_visible` | bool | — | TRUTH: target inside the configured FOV | always |
| 4 | `target_detected` | bool | — | MEASUREMENT: detector returned a centroid | always |
| 5 | `target_position_x_m` | double | m | TRUTH: target world +X (forward) | always |
| 6 | `target_position_y_m` | double | m | TRUTH: target world +Y (right) | always |
| 7 | `target_position_z_m` | double | m | TRUTH: target world +Z (up) | always |
| 8 | `target_velocity_x_mps` | double | m/s | TRUTH: target world velocity X | always |
| 9 | `target_velocity_y_mps` | double | m/s | TRUTH: target world velocity Y | always |
| 10 | `target_velocity_z_mps` | double | m/s | TRUTH: target world velocity Z | always |
| 11 | `detected_x_px` | optional double | px | detected beacon centroid x (image: origin top-left, +x right) | present iff `target_detected` |
| 12 | `detected_y_px` | optional double | px | detected beacon centroid y (+y down) | present iff `target_detected` |
| 13 | `pixel_error_x_px` | optional double | px | `detected_x - cx` (`>0` = beacon RIGHT of centre) | present iff a `TrackingError` exists |
| 14 | `pixel_error_y_px` | optional double | px | `detected_y - cy` (`>0` = beacon BELOW centre) | present iff a `TrackingError` exists |
| 15 | `angular_error_pan_rad` | optional double | rad | `atan(pixel_error_x / fx)` (`>0` = command pan right) | present iff a `TrackingError` exists |
| 16 | `angular_error_tilt_rad` | optional double | rad | `-atan(pixel_error_y / fy)` (`>0` = command tilt up) | present iff a `TrackingError` exists |
| 17 | `angular_error_total_rad` | optional double | rad | `hypot(angular_error_pan_rad, angular_error_tilt_rad)` | present iff a `TrackingError` exists |
| 18 | `camera_pan_rad` | double | rad | camera pan that produced this frame's observation (pre-step) | always |
| 19 | `camera_tilt_rad` | double | rad | camera tilt that produced this frame's observation (pre-step) | always |
| 20 | `command_pan_rate_rad_s` | double | rad/s | PID pan-rate command (`0` on loss / open-loop) | always |
| 21 | `command_tilt_rate_rad_s` | double | rad/s | PID tilt-rate command (`0` on loss / open-loop) | always |
| 22 | `applied_pan_rate_rad_s` | double | rad/s | pan rate the actuator applied (after rate saturation) | always |
| 23 | `applied_tilt_rate_rad_s` | double | rad/s | tilt rate the actuator applied (after rate saturation) | always |
| 24 | `pan_saturated` | bool | — | pan axis at the actuator rate limit this frame (`\|command_pan_rate\| >= max_pan_rate - 1e-9`) | always |
| 25 | `tilt_saturated` | bool | — | tilt axis at the actuator rate limit this frame | always |
| 26 | `detection_error_px` | optional double | px | DIAGNOSTIC: `‖detected centroid − exact projection‖` (truth used only here) | present iff both a detection and a `Visible` projection exist |
| 27 | `tracking_state` | enum string | — | `Tracking` (a `TrackingError` was produced) or `TargetLost` (no detection) | always |
| 28 | `perception_mode` | enum string | — | `CLASSICAL` \| `AI` \| `HYBRID` — which `fsoc::PerceptionMode` this run used (Stage 3, ADR-018) | always (constant per run) |
| 29 | `perception_source` | enum string | — | `NONE` \| `CLASSICAL` \| `AI` \| `HYBRID_AGREEMENT` — which candidate produced the control-facing detection this frame | always |
| 30 | `ai_candidate_detected` | bool | — | the AI detector produced a threshold-accepted candidate this frame (`presence_probability >= 0.95`) | always |
| 31 | `ai_presence_probability` | optional double | — | `sigmoid(presence_logit)` of the AI candidate | present iff `ai_candidate_detected` |
| 32 | `ai_inference_ms` | optional double | ms | AI detector preprocess+forward+decode wall time | present iff `ai_candidate_detected` |
| 33 | `classical_ai_distance_px` | optional double | px | `‖classical centroid − AI centroid‖`, used against the frozen 8.0 px `agreement_radius_px` | present iff both classical and AI produced a candidate this frame |
| 34 | `perception_rejection_reason` | enum string | — | `NOT_APPLICABLE` \| `AI_ONLY_UNVERIFIED` \| `DETECTOR_DISAGREEMENT` — meaningful iff `tracking_state == TargetLost` under `HYBRID` (ADR-018) | always |
| 35 | `tracker_lock_state` | enum string | — | `fsoc::LockState` — `SEARCHING` \| `ACQUIRING` \| `TRACKING` \| `COASTING` \| `LOST` (see `fsoc/target_tracker.hpp`) | always (constant `SEARCHING` when `tracker_enabled` is false) |
| 36 | `tracker_x_px` | optional double | px | alpha-beta filter position estimate x | present iff `tracker_lock_state` is `ACQUIRING`, `TRACKING`, or `COASTING` |
| 37 | `tracker_y_px` | optional double | px | alpha-beta filter position estimate y | present under the same condition |
| 38 | `tracker_vx_px_s` | optional double | px/s | alpha-beta filter velocity estimate x | present under the same condition |
| 39 | `tracker_vy_px_s` | optional double | px/s | alpha-beta filter velocity estimate y | present under the same condition |
| 40 | `tracker_confidence` | optional double | — | in `[0,1]`; `1.0` on a fresh accepted measurement, decaying each coasted frame | present under the same condition |
| 41 | `tracker_is_prediction` | bool | — | `true` iff this frame's tracker position came from prediction (coasting), not a real accepted measurement | always |
| 42 | `tracker_coast_frames` | size_t | — | consecutive frames currently coasting (`0` when not coasting) | always |

Fields 28-34 are additive (Stage 3, `feat/ai-perception`) and DIAGNOSTIC ONLY — the
PID never reads them. For every pre-Stage-3 run (and every run using the default
`PerceptionMode::Classical`), `perception_mode` is the constant string `CLASSICAL` and
`perception_source` mirrors `tracking_state` exactly (`CLASSICAL` iff `Tracking`, `NONE`
iff `TargetLost`) — a reader that ignores columns 28-34 sees the exact same 27-column
schema as before.

Fields 35-42 are additive (P0-v2, `TargetTracker`) and DIAGNOSTIC ONLY — see
`include/fsoc/target_tracker.hpp` for the estimator itself and `is_safe_to_steer()` for
the one rule that lets a coasted (predicted) position actually reach `result.detection`
(and therefore the control-facing `detected_x_px`/`detected_y_px`/`tracking_state`
columns above) during a brief dropout. For every run with `tracker_enabled` at its
default (`false`), the tracker is never constructed, `tracker_lock_state` is the
constant string `SEARCHING` for every frame, `tracker_x_px`/`tracker_y_px`/
`tracker_vx_px_s`/`tracker_vy_px_s`/`tracker_confidence` are always empty, and
`tracker_is_prediction`/`tracker_coast_frames` are always `0` — a reader that ignores
columns 35-42 sees the exact same 34-column schema as before.

## Tracking state

`enum class TrackingState { Tracking, TargetLost }`. Deliberately two-valued: the runner
has no deterministic acquisition phase, and "slewing flat-out while tracking" is already
carried by `pan_saturated` / `tilt_saturated`. An `Acquiring` state would be decorative.

## CSV

* Written by `fsoc::CsvTelemetryLogger` — synchronous `std::ofstream`, one line per record,
  flushed after every line. No threads, no async queue, no external CSV dependency.
* Header line = the 42 column names above, comma-separated, in order.
  `CsvTelemetryLogger::column_names()` is the single source of that order (also the stable
  JSON key list for the frontend — `frontend/lib/telemetry/normalize.ts` parses by header
  name, so it is unaffected by columns appended after it last read the file).
* Every record line has exactly 42 comma-separated fields (empty fields count).
* Logs are written to `generated/` (git-ignored); binary/CSV logs are never committed.

## Benchmark metrics — denominator conventions

`fsoc::BenchmarkMetrics` / `compute_benchmark_metrics(records, wall_execution_time_s)`:

* **Angular and pixel error** metrics (`rms_/mean_/max_/final_/p95_angular_error_rad`,
  `mean_/rms_/max_pixel_error_px`): computed **only over frames with a `TrackingError`**
  (`tracking_state == Tracking`). Denominator = `tracking_frames`. Pixel error uses the
  magnitude `hypot(pixel_error_x, pixel_error_y)`.
* **`mean_detection_error_px`**: over frames where `detection_error_px` is present.
* **Saturation fractions** and **rate means / peaks**: over **all** frames.
* **`detection_fraction`** = `detected_frames / frames`.
* **`final_angular_error_rad`** = the total angular error of the chronologically last
  tracking frame.
* **95th percentile** (`p95_angular_error_rad`): nearest-rank on the sorted-ascending list
  of `angular_error_total_rad` magnitudes — `index = ceil(0.95 * N) - 1`, clamped to
  `[0, N-1]`. No statistics library.

## Wall-clock vs simulation-clock

* `simulation_time_s` and all physics use the **fixed** `dt = 0.02 s` (50 Hz). This is
  never derived from wall-clock time and is fully replayable.
* `wall_execution_time_s` and `processing_fps = frames / wall_execution_time_s` are
  **measured separately** with `std::chrono::steady_clock` around the step loop only
  (telemetry conversion and CSV I/O are excluded). `processing_fps` is the throughput of
  the machine, **not** the 50 Hz simulated camera rate; the two are reported distinctly.
