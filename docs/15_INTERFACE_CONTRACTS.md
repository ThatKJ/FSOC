# Interface Contracts

Planned contracts should preserve this information flow:

```cpp
TargetState trajectory.state_at(double sim_time_s);   // { position_m, velocity_mps }
std::optional<Projection> camera.project(Vec3 target_world_m);
CameraObservation observe_beacon(const PanTiltCamera&, Vec3 beacon_world_position_m);
cv::Mat SyntheticCameraRenderer::render(const CameraObservation& observation);  // CV_8UC1
std::optional<BeaconDetection> BeaconDetector::detect(const cv::Mat& frame);    // pixels only
std::optional<TrackingError> compute_tracking_error(
    const std::optional<BeaconDetection>&, const PanTiltCamera&);
ControlCommand PIDController::update(const TrackingError&, double dt_s);  // rad/s rates
AppliedRates camera.step(command.pan_rate_rad_s, command.tilt_rate_rad_s, dt_s);
telemetry.record(...);
```

The exact types may evolve, but dependencies must point in this direction. In particular:
- trajectory never owns/advances the simulation clock (the runner passes absolute `sim_time_s`) and knows nothing of camera/perception/control,
- detector never receives world truth,
- controller never receives target XYZ,
- camera never owns PID gains,
- telemetry never controls behavior.

`Trajectory::state_at` contract: `sim_time_s` must be finite and `>= 0`; violations throw
`std::invalid_argument`. It never returns NaN/Inf for valid input and is deterministic
(repeated calls with equal argument return bit-identical state).

## Observation / measurement / tracking-error contracts (Step 3, frozen)

Layers stay distinct — do not alias them:

| type | concept | knows about |
|---|---|---|
| `TargetState` | physical truth | world only |
| `Projection` / `CameraObservation` | exact camera projection | camera + a world point |
| `BeaconDetection` | estimated image measurement | image pixels only |
| `TrackingError` | controller-facing error | detection + camera intrinsics |

- **Image coordinate convention:** origin top-left, `+x_px` right, `+y_px` down. Centre is
  `cx = width_px / 2.0`, `cy = height_px / 2.0`, owned by `PanTiltCamera::cx_px()/cy_px()`.
  No other module may redefine the centre.
- **Visibility:** `ObservationStatus { Visible, OutsideFieldOfView, BehindCamera }`. The
  image point is present iff `Visible`. Depth test (`z_cam <= 0`) precedes the FOV test.
- **Lost target:** represented only by an empty `std::optional`. No `(-1,-1)`, no NaN, no
  zero sentinel. `compute_tracking_error(std::nullopt, …) == std::nullopt`.
- **Pixel error sign:** `x_px = detected_x - cx` (`>0` = beacon RIGHT), `y_px = detected_y - cy`
  (`>0` = beacon BELOW, because image `+y` is down).
- **Angular error sign:** `pan_rad = atan(x_px / fx)` (`>0` = beacon right → command pan
  right); `tilt_rad = -atan(y_px / fy)` (`>0` = beacon above → command tilt up). Produced by
  the single existing routine `PanTiltCamera::pixel_error_to_angles`; never re-derived.
- **Validation:** non-finite detection centroid → `std::invalid_argument`. Invalid image
  dimensions rejected by `CameraConfig::validate()`. `ScenarioConfig::validate()` requires
  finite `duration_s > 0`, finite `timestep_s > 0`, `timestep_s <= duration_s`.
- `TrackingError` is a pure image-plane quantity: it does not depend on camera pan/tilt pose.

## Synthetic image renderer contract (Step 4, frozen)

- **First OpenCV boundary.** OpenCV appears only in `fsoc/renderer.hpp` + `src/renderer.cpp`,
  built as a separate `fsoc_render` library. `fsoc_core` and every pure-math header
  (`geometry.hpp`, `camera.hpp`, `target_state.hpp`, `trajectory.hpp`, `observation.hpp`,
  `tracking_error.hpp`, `scenario.hpp`) stay OpenCV-free.
- **Input / output:** `SyntheticCameraRenderer::render(const CameraObservation&) -> cv::Mat`,
  always `CV_8UC1` of the configured size. The renderer sees only the observation — never
  `TargetState`, world coordinates, trajectory, or pan/tilt state.
- **Image model:** uniform background (`background_intensity`, default 5) plus an analytic
  2-D Gaussian beacon,
  `I(u,v) = background + peak·exp(-((u-u0)² + (v-v0)²)/(2σ²))`, clamped to `[0,255]` and
  rounded. `peak` default 255. **σ is in pixels.**
- **Pixel-sampling convention:** pixel `(u,v)` is sampled at continuous coordinate `(u,v)`
  (pixel centres at integer coords, consistent with `cx = W/2.0`).
- **Sub-pixel:** the beacon centre `(u0,v0)` is the fractional `ImagePoint` value; it is
  never rounded before evaluating the Gaussian, so a weighted centroid recovers the
  requested sub-pixel location.
- **Status behaviour:** `Visible` → beacon rendered; `OutsideFieldOfView` / `BehindCamera`
  → background-only frame (never a fabricated beacon).
- **Edge safety:** the Gaussian is evaluated in a raster window clipped to the image; a
  near- or off-edge beacon cannot index out of bounds.
- **Validation:** `RendererConfig::validate()` rejects `width_px<=0`, `height_px<=0`,
  non-finite/`<=0` `beacon_sigma_px`, `beacon_window_sigmas<1`. `render()` throws
  `std::invalid_argument` if `status==Visible` but the image point is missing or non-finite.
- **Dimensions authority:** the camera. Build `RendererConfig` via
  `renderer_config_for(const CameraConfig&, ...)` so renderer and camera cannot disagree.
- **No noise this step.** Deterministic: the same observation renders byte-identical frames.

## Baseline beacon detector contract (Step 5, frozen)

- **Module boundary.** The detector lives in `fsoc_perception` (`fsoc/detector.hpp` +
  `src/detector.cpp`), linking `fsoc::core` + OpenCV `core`/`imgproc`. It **must not**
  depend on `fsoc_render` and its headers include **neither** `renderer.hpp` **nor**
  `observation.hpp`. `detect()` takes only `const cv::Mat&` — never `TargetState`,
  trajectory, `CameraObservation`, the exact projected `ImagePoint`, `TrackingError`, or
  controller state. It works on any valid `CV_8UC1` frame, not only renderer output.
- **Input validation.** `detect()` throws `std::invalid_argument` for an empty `cv::Mat`
  or any type other than `CV_8UC1` (RGB / 16-bit / float frames are rejected, not
  reinterpreted). `BeaconDetectorConfig::validate()` requires
  `threshold_intensity ∈ [1, 254]` and `min_bright_pixels ≥ 1`.
- **Threshold rule.** A pixel is a candidate iff `value >= threshold_intensity`
  (default 64 — a generic bright cut, not tied to the renderer's background).
- **Component selection.** 8-connected components on the candidate mask; components with
  `area < min_bright_pixels` are dropped; the surviving component with the greatest
  **integrated signal** `Σ weight` wins. Ties resolve to the lowest label (first in raster
  order) — deterministic.
- **Centroid math.** Over the winning component's pixels only:
  `weight(p) = (pixel(p) − threshold_intensity) + 1` (always ≥ 1);
  `centroid = Σ weight·pos / Σ weight`. Subtracting the threshold (not an assumed
  background) removes the pedestal so the estimate tracks the true sub-pixel centre;
  restricting to one component stops a second bright region from pulling it.
- **Sub-pixel gate.** On clean interior synthetic frames, `|Δx|, |Δy| ≤ 0.15 px`
  (achieved ≈ 0.02 px). Edge-clipped beacons are found safely but carry a bias.
- **Lost target.** `std::nullopt` when no pixel passes the threshold or no component is
  large enough. No `(-1,-1)`, NaN, or zero sentinel — the Step-3 contract holds.
- **Deterministic.** The same frame yields a bit-identical centroid.
- `BeaconDetection` stays centroid-only: no fabricated confidence.

## Pan/tilt PID controller contract (Step 6, frozen)

- **Module boundary.** The controller lives in `fsoc_control` (`fsoc/pid_controller.hpp` +
  `src/pid_controller.cpp`), linking **`fsoc::core` only** (via `fsoc/tracking_error.hpp`
  for the `TrackingError` contract). It is **OpenCV-free** and must not link or include
  `fsoc_render` / `fsoc_perception` / OpenCV. `update()` consumes only `TrackingError`
  (radians) + `dt_s` — never `TargetState`, target XYZ, trajectory, `CameraObservation`,
  `cv::Mat`, `BeaconDetection`, a projected `ImagePoint`, or `PanTiltCamera`.
- **Output.** `ControlCommand { double pan_rate_rad_s; double tilt_rate_rad_s; }` —
  desired actuator angular **velocity**, rad/s. Never absolute angles; the PID never
  touches `PanTiltCamera` (actuation is Step 7). `zero_control_command()` is the explicit
  hold-still value the runner uses on target loss.
- **Law (per axis, discrete).** `e` = `angular.pan_rad` / `angular.tilt_rad`;
  `D = (e − e_prev)/dt_s`, forced to `0` on the first `update()` after construction/`reset()`;
  `I += e·dt_s`; `u = kp·e + ki·I + kd·D`; `command = clamp(u, ±output_limit_rad_s)`.
- **Anti-windup.** (1) `I` is hard-clamped to `±integral_limit` every update. (2) Conditional
  integration: if `u` is beyond `±output_limit_rad_s` **and** `e` has the same sign as that
  saturation, the sample is not accumulated (`I` held). Any other case commits the clamped
  accumulation, so `I` unwinds as soon as `e` reverses.
- **Sign (frozen).** `e > 0` (beacon RIGHT / ABOVE) with `kp > 0` ⇒ `command > 0` ⇒ pan
  right / tilt up. Neither axis is inverted. Positive error → positive rate, which drives
  the axis toward the beacon under `PanTiltCamera::step`.
- **Reset.** `reset()` clears both integrals, both previous errors, and both first-sample
  flags; the next `update()` behaves exactly like the first.
- **Validation.** `update()` throws `std::invalid_argument` if `dt_s` is non-finite or
  `≤ 0`, or if any `TrackingError` component is non-finite — **state is not mutated on
  throw**. `PIDControllerConfig::validate()` requires finite `kp, ki, kd, integral_limit ≥ 0`
  and finite `output_limit_rad_s > 0` on both axes.
- **Axes independent.** Pan and tilt each own their integral / previous-error / first-sample
  state; one axis saturating or winding up does not affect the other.
- **Deterministic, allocation-free.** The same input sequence from `reset()` yields a
  bit-identical command sequence; `update()` performs no heap allocation.
- **Gains.** `PIDControllerConfig{}` defaults are **untuned baseline placeholders** — to be
  tuned during the Step-7 closed-loop experiments. `output_limit_rad_s` defaults to
  `deg_to_rad(30)` (the default `CameraConfig` actuator rate); the Step-7 runner is
  responsible for keeping controller and actuator limits consistent.

## Closed-loop SimulationRunner contract (Step 7, frozen)

- **The one integration layer.** `fsoc_simulation` (`fsoc/simulation_runner.hpp` +
  `src/simulation_runner.cpp`) links `fsoc::core` + `fsoc::render` + `fsoc::perception` +
  `fsoc::control`. This is the **only** place those modules come together; the lower
  libraries stay independent of each other. The runner owns the clock, the fixed timestep,
  subsystem call order, the target-loss policy, and camera stepping — and **no domain
  math** (it duplicates no projection / trajectory / centroid / PID / pixel→angle code).
- **`step()` order (frozen).** `trajectory.state_at(sim_time)` → `observe_beacon(camera, pos)`
  → `renderer.render(observation)` → `detector.detect(frame)` →
  `compute_tracking_error(detection, camera)` → if error present `pid.update(error, dt)`
  else `pid.reset()` + `zero_control_command()` → `camera.step(cmd.pan, cmd.tilt, dt)` →
  record `SimulationStepResult` → `sim_time += dt`, `++frame_index`. The camera is **not**
  moved before the frame is computed.
- **Fixed timestep.** `timestep_s` (default 0.02 s = 50 Hz), advanced by `sim_time += dt`.
  **Never wall-clock.** `std::chrono` may measure execution speed but never the simulation
  dt. Fully replayable: same config + trajectory → bit-identical `SimulationStepResult`
  sequence.
- **TRUTH vs MEASUREMENT (frozen).** The control feedback path is exactly
  `detector.detect(cv::Mat)` → `compute_tracking_error(detection, camera)` → `pid.update`.
  `TargetState.position_m/velocity_mps`, `CameraObservation.image_point_px`, and the exact
  `Projection` are used **only** for the diagnostic fields of `SimulationStepResult`
  (labelled "truth") and for `detection_error_px` scoring. None ever reaches the controller.
  Verified structurally (the runner reads `observation.image_point_px` only in the scoring
  block) and executably (`test_control_follows_detected_not_truth`).
- **Target-loss policy.** No detection ⇒ `tracking_error == std::nullopt` ⇒ `pid.reset()`,
  `command == {0,0}`, `camera.step(0,0,dt)` (camera holds its orientation). No search /
  reacquisition. If target motion brings the beacon back into the FOV, the PID resumes from
  its reset state.
- **Trajectory injection.** `SimulationRunner(SimulationRunnerConfig, const Trajectory&)` —
  any `Trajectory` subclass; the runner holds it by reference (caller keeps it alive) and
  never hardcodes a concrete trajectory.
- **Camera authority.** The runner constructs/owns `PanTiltCamera` and never duplicates its
  pan/tilt state; `reset()` reconstructs it at the configured initial pose.
- **Controller/actuator consistency.** `SimulationRunnerConfig::validate()` rejects a
  configuration whose PID `output_limit_rad_s` exceeds the camera's `max_*_rate_rad_s`, and
  requires renderer dimensions to equal the camera's. The MVP baseline sets them equal.
- **Baseline gains (empirically tuned, NOT optimal).** `baseline_runner_config()` uses
  kp = 12, ki = 0, kd = 0 on both axes (P-dominant: the plant angle is the integral of the
  rate command, so P alone gives a first-order, non-oscillatory response with zero
  steady-state error for a stationary target). Output limit = `deg_to_rad(30)`.
- **Performance gates.** Static acquisition: 100 % detection after frame 0, final total
  angular error < 0.05° (achieved ≈ 0°), final centroid < 2 px from centre. Linear:
  ≥ 95 % detection, RMS angular error < 0.75° (achieved ≈ 0.39°). Sinusoidal (±12.4°
  swing): ≥ 95 % detection, RMS < 1.0° (achieved ≈ 0.55°), max < 1.5°. Closed-loop must
  beat open-loop on both detection fraction and RMS on the same trajectory (achieved
  57 % → 100 %, 6.45° → 0.55°).

## Telemetry + benchmarking contract (Step 8, frozen)

- **Observer only.** `fsoc_telemetry` (`fsoc/telemetry.hpp` + `src/telemetry.cpp`) depends
  on `fsoc::simulation`. It is a SINK: `make_telemetry_record(...)`,
  `compute_benchmark_metrics(...)`, `CsvTelemetryLogger`, and `run_and_record(...)` consume
  `SimulationStepResult` values and never call back into the runner / PID / camera /
  detector / renderer / trajectory. Running a simulation with or without telemetry yields a
  **bit-identical** `SimulationStepResult` sequence (`test_telemetry_non_interference`).
- **`TelemetryRecord`** — 27 flat, unit-suffixed, JSON-mappable fields (full table in
  `docs/08_TELEMETRY_SCHEMA.md`). Unavailable measurements are `std::optional<double>` /
  `std::nullopt` **in memory — never a `-1` / NaN / `N/A` sentinel** — and **empty fields**
  in CSV. `make_telemetry_record` takes the actuator rate limits so the `*_saturated` flags
  can be set without the record carrying the whole camera config.
- **`TrackingState`** — `{ Tracking, TargetLost }` only. Present in every record;
  `Tracking` iff the frame produced a `TrackingError`.
- **`*_saturated`** — true when `|command_*_rate| ≥ max_*_rate − 1e-9` (axis slewing at the
  actuator limit); in the baseline the PID output limit equals the actuator rate, so this
  is equivalent to "PID output saturated" / "camera clipped the command".
- **`CsvTelemetryLogger`** — synchronous `std::ofstream`, header from
  `column_names()` (the one source of column order), one line per record flushed to disk,
  no threads / async / external CSV library. Output goes to `generated/` (git-ignored);
  logs are never committed.
- **`BenchmarkMetrics`** — see `docs/08_TELEMETRY_SCHEMA.md` for the full list.
  **Denominators:** angular/pixel error metrics over frames with a `TrackingError`
  (`tracking_frames`); `mean_detection_error_px` over frames with a `detection_error_px`;
  saturation fractions and rate means/peaks over all frames; `detection_fraction =
  detected_frames / frames`. **95th percentile:** nearest-rank on the sorted-ascending
  magnitudes, `index = ceil(0.95·N) − 1` clamped to `[0, N-1]`; no statistics library.
- **Wall clock vs simulation clock.** Physics uses the fixed `dt = 0.02 s` (50 Hz) only.
  `wall_execution_time_s` / `processing_fps = frames / wall_execution_time_s` are measured
  with `std::chrono::steady_clock` around the step loop **only** (telemetry conversion and
  CSV I/O excluded) and never feed the simulation timestep. The two rates are reported
  separately (`50 Hz` simulation vs `~4700 FPS` processing / `~90×` real time).

## Engineering visualization contract (Step 9, frozen)

- **Observer only.** `fsoc_visualization` (`fsoc/visualization.hpp` + `src/visualization.cpp`)
  depends on `fsoc::simulation` + `fsoc::telemetry` + OpenCV core/imgproc/imgcodecs (and
  `videoio` iff present). `TrackingVisualizer::annotate()` reads a `SimulationStepResult` +
  `TelemetryRecord` and **never** calls back into the runner / detector / PID / camera /
  trajectory. Running a simulation with vs without visualization yields a bit-identical
  `SimulationStepResult` sequence (`test_visualization_non_interference`, 500 frames).
- **The perception frame is untouched.** `annotate(const cv::Mat& grayscale_frame, …)` takes
  the `CV_8UC1` frame by const reference and leaves it **byte-for-byte identical**; it
  returns a **new `CV_8UC3` BGR** image of the same size (`cv::cvtColor(GRAY2BGR)` into a
  fresh buffer, then draws). Overlay pixels can never reach the detector. Empty /
  non-`CV_8UC1` input → `std::invalid_argument`.
- **Base-frame reconstruction — no runner change.** `SimulationRunner` /
  `SimulationStepResult` are unchanged. The base frame for a result is
  `SyntheticCameraRenderer{config.renderer}.render(result.observation)` — byte-identical to
  the frame the runner detected on (the renderer is deterministic, Step 4). No `cv::Mat` is
  stored in `SimulationStepResult` / `TelemetryRecord`.
- **Overlays** (full table + colour semantics in `docs/09_VISUALIZATION.md`): centre
  crosshair from `(cols/2, rows/2)` (not hardcoded); detection marker at
  `telemetry.detected_*` (absent when `TargetLost` — no fake marker); centre→detected error
  vector (shown iff a `TrackingError` exists; shrinks to zero on convergence); `TRACKING` /
  `TARGET LOST` from `telemetry.tracking_state`; `VISIBLE` vs `DETECTED` (distinct); SIM /
  FRAME; PAN / TILT / ANG ERR / ERR PX; CMD rates with amber `RATE LIMIT` driven by the
  Step-8 `*_saturated` flags. HUD values are degrees for humans; physics stays radians.
- **Colours (BGR, fixed):** green = tracking, red = target lost, amber = saturation/limit,
  white-grey = crosshair/neutral data, cyan = optional TRUTH marker.
- **Truth vs detection.** The default demo view emphasises the **detected** centroid. The
  exact projection (`DETECT ERR` line and `TRUTH` square marker) is **off by default**; when
  enabled it is a visibly distinct labelled marker and is never fed into control.
- **Headless.** PNG per selected frame (`write_png`) is the required portable path (no
  `cv::imshow` / `cv::waitKey`). `try_write_mp4` is best-effort: returns `false` without
  throwing when there is no `videoio` / codec / backend, and leaves no partial file. Output
  → `generated/` (git-ignored); no images committed.
- `VisualizationConfig` gates every overlay independently; all off → `annotate()` returns a
  plain `cvtColor(GRAY2BGR)` of the input.

## Baseline validation contract (Step 10, frozen)

- **Evaluation layer, not a component.** `fsoc_validation` (`fsoc/validation.hpp` +
  `src/validation.cpp`) depends on `fsoc::simulation` + `fsoc::telemetry` +
  `fsoc::visualization`. It **runs the existing v1 system and reads its outputs**. It
  implements **no** trajectory / detector / PID / renderer / camera / pixel→angle math,
  never calls a control path, never advances the loop itself, and never mutates a
  `SimulationRunnerConfig` to make a number look better. Creating a `Trajectory` for a
  named scenario is the only "domain" thing it does. Steps 1–9 sources are unchanged.
- **Acceptance gates are frozen before the run.** Every threshold is defined and justified
  in `docs/16_BASELINE_ACCEPTANCE.md` ahead of evaluation; none is computed from the run
  being scored. The suite reports a failing scenario — it does **not** loosen a gate, retry
  with different gains, or re-tune the controller. The baseline PID stays kp = 12, ki = 0,
  kd = 0 (ADR-010).
- **API.** `ValidationScenarioId` (7 values, `to_string`); `AcceptanceCheck {name, passed,
  actual, limit, unit, comparator}`; `ValidationResult {id, scenario_name, description,
  BenchmarkMetrics metrics, vector<AcceptanceCheck> checks, bool passed, bool deterministic,
  csv_path, evidence_images, open_loop_metrics, has_open_loop_comparison}`;
  `ValidationSuiteResult {vector<ValidationResult> scenarios, bool overall_passed}`.
  `evaluate_passed(result)` ⇔ every check in `result.checks` passed; `overall_passed` ⇔
  every scenario `passed` (`std::all_of`). `ValidationSuite{evidence_dir}` — `run_all()`
  plus one `run_*()` per scenario; an **empty** `evidence_dir` runs the scenarios with no
  CSV/PNG/report I/O (used by the unit tests). `write_report(result)` emits
  `<evidence_dir>/VALIDATION_REPORT.md`.
- **Global checks appended to every scenario** (`global_acceptance_checks`): frame count
  > 0; timestamps strictly monotonic; `max |t[i] − i·dt| ≤ 1e-9 s`; every telemetry scalar
  / present optional finite (no NaN/Inf); `max |command rate| ≤ PID output limit`;
  `max |applied rate| ≤ camera actuator limit`; target-loss semantics (every lost frame:
  zero command, no `TrackingError`, `TrackingState::TargetLost`, measurement optionals
  empty).
- **Determinism.** Each scenario is run twice through an independent `SimulationRunner`;
  `deterministic` ⇔ the two `BenchmarkMetrics` (excluding wall-clock / FPS) **and** the two
  `SimulationStepResult` sequences are bit-identical. The simulation carries no RNG.
- **The evaluator can fail.** `test_failure_check_is_real` (mandatory) injects an impossible
  `AcceptanceCheck` and, separately, tightens a real threshold past its measured value, and
  asserts `evaluate_passed()` and `ValidationSuiteResult::overall_passed` both become
  `false`. Step 10 is not a decorative always-green harness.
- **Evidence.** Per-scenario 34-column `TelemetryRecord` CSV (27 core + 7 Stage-3 perception fields) via `CsvTelemetryLogger`;
  annotated PNGs via the Step-9 observer path only —
  `SyntheticCameraRenderer{config.renderer}.render(result.observation)` for the base frame,
  then `TrackingVisualizer::annotate()` — no drawing code is re-implemented.
  `VALIDATION_REPORT.md` values are generated from the run, never hardcoded. All artifacts
  go to `generated/step10/` (git-ignored); `docs/16_BASELINE_ACCEPTANCE.md` is the only
  committed Step-10 evidence document.
- **`step10_validation_smoke`** ends with exactly `STEP 10 BASELINE ACCEPTANCE: PASS`
  (exit 0) iff `overall_passed && report written`, otherwise
  `STEP 10 BASELINE ACCEPTANCE: FAIL` (exit 1). Suite wall time is printed as
  informational only — no ephemeral FPS figure is a permanent gate.

## Demo packaging contract (Step 11, additive — does not touch the baseline)

- **Presentation layer, not a component.** `fsoc_demo_support` (`fsoc/demo.hpp` +
  `src/demo.cpp`) links `fsoc::simulation` + `fsoc::telemetry`. It packages the FROZEN
  `v1_baseline` engine for the SIH demo and a future web frontend. It implements **no**
  trajectory / detector / PID / renderer / camera / pixel→angle math, never enters a
  control path, and adds **no** networking, HTTP, WebSocket, or JSON dependency. It does
  **not** modify geometry, camera, trajectory, renderer, detector, `tracking_error`,
  `pid_controller`, or `simulation_runner` — verified by `git diff`.
- **`DemoScenario`** — `StaticAcquisition` / `SinusoidalTracking` / `LossReacquisition` /
  `OpenLoop` / `ClosedLoop`. `to_string` gives the SCREAMING_SNAKE name;
  `demo_scenario_token` gives the CLI token (`static` / `sinusoidal` / `loss` / `open` /
  `closed`); `parse_demo_scenario(std::string_view)` accepts either form, case-sensitive,
  and returns `std::nullopt` for anything else (the CLI turns that into a clean exit-2
  usage error). `DemoScenario` is **not** a replacement for `ValidationScenarioId` — it is
  presentation selection only.
- **`DemoScenarioPlan`** (`make_demo_scenario_plan`) is the single place a scenario expands
  into `{ SimulationRunnerConfig, std::unique_ptr<Trajectory>, duration_s }`. The parameter
  values are copied verbatim from `src/validation.cpp` (`run_static_acquisition` /
  `run_sinusoidal` / `run_loss_and_reentry` / `run_open_vs_closed`) and are **never**
  retuned. `OpenLoop` and `ClosedLoop` share byte-identical `SinusoidalTrajectory`
  parameters and `initial_tilt_rad`; only `control_enabled` differs.
- **`DemoSnapshot`** — a per-frame COPY / VIEW MODEL produced by
  `make_demo_snapshot(const SimulationStepResult&, const TelemetryRecord&, const
  CameraConfig&)`. Clock, frame index, target truth, camera pose, applied rates and the raw
  PID command come from the `SimulationStepResult`; the tracking-state mirror, detected
  centroid, pixel/angular errors and saturation flags come from the `TelemetryRecord`; the
  FOV comes from the `CameraConfig`. Absent measurements are `std::optional` = `std::nullopt`
  — **no `-1` / `NaN` / sentinel**. The snapshot is never read by the control loop
  (`test_non_interference`). Full field table + the future JSON shape:
  `docs/18_FRONTEND_DATA_CONTRACT.md`.
- **Units (frozen boundary).** The core and `DemoSnapshot` are **radians / rad·s⁻¹ / m /
  m·s⁻¹ / px**. Degrees appear **only** through `to_degrees(const DemoSnapshot&) ->
  DemoSnapshotAnglesDeg`, which converts the angular quantities (pan, tilt, their rates,
  FOVs, optional angular errors, command rates) for a UI consumer. Pixels and metres pass
  through unchanged. Core physics units are not modified.
- **`DemoTrackingState`** mirrors `fsoc::TrackingState` 1:1 (`Tracking` / `TargetLost`;
  `to_string` → `"TRACKING"` / `"TARGET_LOST"`) so the frontend contract does not depend on
  a telemetry header. **`DemoRunState`** (`Ready` / `Running` / `Paused` / `Finished`) is
  application/session state and is kept separate — `Paused` is not a kind of `TargetLost`.
- **`DemoSession`** — composition over the validated stack. Member order (frozen for
  lifetime safety): the `std::unique_ptr<Trajectory>` is declared and constructed **before**
  the `SimulationRunner`, so the heap trajectory outlives the runner that references it; no
  temporary trajectory is ever passed. `step()` advances exactly one fixed 50 Hz timestep
  and returns that frame's snapshot; while `Paused` or `Finished` it advances **nothing**
  (no hidden simulation — `frame_index()` / `simulation_time_s()` do not move). `reset()`
  returns to `t = 0`, frame 0, camera at the initial pose, PID reset, scenario preserved —
  the next run is bit-identical. Two sessions of one scenario emit identical snapshot
  sequences. `last_snapshot()` / `last_telemetry()` / `last_step_result()` expose the three
  views of the current frame; the CLI and the non-interference test use the latter two.
- **Non-interference (mandatory).** For every `DemoScenario`, a bare `SimulationRunner`
  built from the same `DemoScenarioPlan` and a `DemoSession` produce **field-identical**
  `SimulationStepResult` sequences (`simulation_time_s`, `frame_index`, `camera_pan/tilt_rad`,
  `command`, `applied_rates`, detection presence + centroid, tracking-error presence +
  angles, target truth). Packaging the engine for the demo cannot change closed-loop
  behaviour.
- **`fsoc_demo`** (`apps/fsoc_demo.cpp`) — `fsoc_demo <token> [--duration <s>] [--csv
  <path>] [--quiet]` / `--help`. Per-frame status lines plus an end-of-run summary
  (detection %, RMS / P95 / max angular error, lost frames) computed with the existing
  Step-8 `compute_benchmark_metrics` — no metrics math is re-implemented. Unknown scenario
  or bad option → usage text on stderr, exit 2. Wall-clock FPS is informational; the
  simulation is always fixed 50 Hz.
- **Reproducibility.** `make demo` / `scripts/run_baseline_demo.sh` (`set -euo pipefail`,
  no destructive git ops, no hardcoded Homebrew paths) runs the Step-10 validation, the
  `static` and `sinusoidal` demos, and the Step-9 visualization evidence, then prints the
  artifact paths under `generated/` (git-ignored).

## V2 AI perception contract (post-`v1_baseline`, additive — full detail in `docs/19`)

- **The controller-facing contract is unchanged.** The learned detector emits the *same*
  `std::optional<BeaconDetection>` (centroid-only, no fabricated confidence) as the classical
  detector. `compute_tracking_error`, the PID law + gains, `PanTiltCamera`, actuator limits,
  the frozen `SimulationRunner::step()` order, and the Step-10 gates are untouched.
- **`fsoc_ai_datagen`** (`fsoc/ai_frame_synth.hpp` + `src/ai_frame_synth.cpp`) — dataset
  tooling only, **not** in the control path. Links `fsoc::core` + OpenCV core/imgproc; like
  `fsoc_perception` it must **not** depend on `fsoc_render`. `AiFrameSynthesizer::synthesize(
  seed)` is a pure, portable, byte-reproducible function (all randomness via
  `std::mt19937_64`). It re-uses the analytic Gaussian beacon model, never the renderer.
- **`generate_ai_dataset`** (`apps/generate_ai_dataset.cpp`) — writes
  `generated/ai_dataset/{train,val,test}` + JSONL label manifests + `dataset.json`. Splits
  are contiguous, disjoint blocks of one global index space; per-sample seed
  `sample_seed_for(dataset_seed, global_index)` ⇒ no cross-split leakage. Output git-ignored.
- **Learned detector (`fsoc_ai_perception`, later stage).** `AiBeaconDetector::detect(const
  cv::Mat&) → std::optional<AiBeaconDetection>` — pixels only (never `TargetState`,
  trajectory, projected truth, `TrackingError`, controller state). `AiBeaconDetection`
  wraps a `BeaconDetection` plus **diagnostic-only** `confidence` / `peak_confidence` /
  `inference_ms`. Same input validation style as `BeaconDetector` (empty / non-`CV_8UC1`
  → `std::invalid_argument`); constructor fails cleanly on a missing / malformed model.
- **Perception seam.** `enum class PerceptionMode { Classical, AI, Hybrid }` on
  `SimulationRunnerConfig`, **default `Classical`** (bit-identical to v1, regression-tested).
  `enum class PerceptionSource { None, Classical, AI, HybridAgreement }` is **diagnostic
  telemetry only** — never a `TrackingState` / `DemoRunState`, never read by the PID.
- **Safe Hybrid policy (ADR-018, post-Stage-2, documentation only — Stage 3 not started).**
  Under `PerceptionMode::Hybrid`, `PerceptionSource::AI` is **never emitted** — AI never
  independently supplies the control-facing centroid, only confirms (case 1) or is superseded
  by the classical centroid (case 2). An AI-only detection (case 3) and a classical/AI
  disagreement (case 4) both resolve to `std::nullopt` with a diagnostic-only
  `enum class PerceptionRejectionReason { NotApplicable, AiOnlyUnverified, DetectorDisagreement }`
  — never a confidence-based override; the ADR-016 draft's `high_confidence_threshold` escape
  hatch is retired. `agreement_radius_px` is frozen at **8.0 px** (one heatmap cell,
  `INPUT_STRIDE`), not an ML threshold. Full policy table + evidence: `docs/19 §5`,
  `DECISIONS.md` ADR-018. `PerceptionSource::AI` remains meaningful only under the separate,
  explicit, non-default `PerceptionMode::AI` (diagnostic/benchmark mode) — unaffected by ADR-018.
