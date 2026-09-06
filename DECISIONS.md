# Architecture Decision Log

## ADR-001 — Core language: C++20
**Status:** Accepted

The project has migrated from the original Python starter to modern C++20. Reasons: deterministic timing, direct mapping to embedded/robotics/GNC implementation, stronger type boundaries, and better long-term fit for real-time control work.

Consequence: Python packaging files and workflows are forbidden unless this decision is explicitly superseded.

## ADR-002 — Step-1 math has zero third-party dependencies
**Status:** Accepted

The foundation uses small auditable vector primitives. OpenCV is deferred until pixels; Eigen is deferred until matrix-heavy UKF/MPC work.

## ADR-003 — Fixed world/camera coordinate convention
**Status:** Accepted

World +X forward, +Y right, +Z up. Camera +x right, +y up, +z forward. Image v increases downward.

## ADR-004 — Ground truth never feeds operational controller
**Status:** Accepted

Ideal target angles exist only for diagnostics/tests. The baseline controller must operate from image-derived centroid error.

## ADR-005 — Trajectory engine is a pure time -> state map
**Status:** Accepted

The Step-2 target trajectory layer is a stateless mathematical function
`TargetState state_at(double time_s)`, not an `update(dt)` integrator. The simulation
runner owns the clock and passes absolute `time_s`; this keeps replay deterministic and
lets the runner sub-step or resample without the trajectory drifting.

`TargetState` now carries `position_m` and `velocity_mps` (SI, world frame) and lives in
its own header `fsoc/target_state.hpp` so the trajectory layer depends on target truth
without pulling in camera/projection headers. This is the only Step-1 change: the struct
moved out of `environment.hpp` and gained a zero-initialised `velocity_mps`; existing
aggregate initialisation and `Environment` behaviour are unchanged.

Input-validation policy (fail fast, never emit NaN):
- constructor parameters must be finite on every axis; `frequency_hz` and `amplitude_m`
  must be `>= 0` (sign is expressed through `phase_rad`);
- `state_at(time_s)` requires finite, non-negative time;
- all violations throw `std::invalid_argument`.

Sinusoidal frequency is specified in hertz and converted once as `omega = 2*pi*f`;
velocity is the exact analytic derivative `A*omega*cos(omega*t + phi)`.

## ADR-006 — Observation / measurement / tracking-error contracts
**Status:** Accepted

Four distinct types, never aliased into one: `TargetState` (truth), `Projection` /
`CameraObservation` (exact camera projection), `BeaconDetection` (image-pixel estimate),
`TrackingError` (controller-facing error). Dependencies point one way; the future detector
never receives `TargetState` and the future controller never receives world XYZ.

- **Image convention (frozen):** origin top-left, `+x_px` right, `+y_px` down — exactly the
  convention already in `PanTiltCamera::project()`. Image centre is `width_px/2.0`,
  `height_px/2.0`, owned solely by `PanTiltCamera::cx_px()/cy_px()`; no module redefines it.
- **Visibility is a 3-state enum** (`Visible`, `OutsideFieldOfView`, `BehindCamera`), not a
  bare optional, because telemetry and loss handling must tell the two failure modes apart.
  Depth test precedes FOV test.
- **Lost target = empty `std::optional` only.** No `(-1,-1)`, NaN, or zero sentinels. The
  `optional`-in / `optional`-out shape of `compute_tracking_error` makes computing an error
  from a lost target hard to do by accident.
- **Sign conventions (frozen, quadrant-tested):** pixel `error_x>0` = beacon right,
  `error_y>0` = beacon below; angular `pan_rad>0` = command pan right, `tilt_rad>0` =
  command tilt up. Angular conversion reuses `PanTiltCamera::pixel_error_to_angles`; the
  pinhole and pixel->angle equations are not duplicated anywhere.
- **Validation:** non-finite detection centroid throws `std::invalid_argument`; invalid
  image dimensions rejected by existing `CameraConfig::validate()`.
- `TrackingError` is pose-independent (pure image-plane quantity).

No Step 1 / Step 2 source changed. `ScenarioConfig` (trajectory mode, `duration_s`, fixed
`timestep_s`) is added as a pure data contract for the Step 7 runner; it holds no logic.

## ADR-007 — OpenCV enters only through a separate `fsoc_render` library
**Status:** Accepted

Step 4 introduces OpenCV (first allowed by CLAUDE.md rule 4). It is confined to one
translation unit pair, `include/fsoc/renderer.hpp` + `src/renderer.cpp`, compiled into a
**separate `fsoc_render` library** that links `fsoc::core` + OpenCV. `fsoc_core` never
links or includes OpenCV; the pure-math headers stay OpenCV-free. `renderer.hpp` is the
first (and, for now, only) public header that `#include`s `<opencv2/core.hpp>`.

- **Build discovery:** `FSOC_ENABLE_OPENCV` is tri-state `AUTO` (default) | `ON` | `OFF`.
  `AUTO` runs `find_package(OpenCV QUIET COMPONENTS core imgcodecs)` and builds the Step 4
  targets only if found, otherwise prints `brew install opencv` and continues (Steps 1-3
  stay buildable/green without OpenCV). `ON` makes it `REQUIRED`. No Homebrew absolute
  paths anywhere — pure CMake package discovery.
- **Renderer ownership limits:** the renderer owns pixels only. It consumes a
  `CameraObservation` and nothing else — no `TargetState`, world coordinates, trajectory,
  camera pose, control, or estimation. OpenCV owns no simulation state.
- **Image format:** `CV_8UC1`; background 5 counts; beacon = analytic 2-D Gaussian, peak
  255, `sigma` in pixels, evaluated at the true sub-pixel `ImagePoint` (never pre-rounded);
  clipped raster window for edge safety; non-`Visible` observation renders background-only.
- **No sensor noise in Step 4** — deterministic pixels are validated before disturbances;
  any future noise must be optional and explicitly seeded.
- **Dimensions authority is the camera** via `renderer_config_for(const CameraConfig&)`.
- `cv::Mat` is used directly at the perception boundary; it is not wrapped.

Generated debug PNGs go to `generated/` (git-ignored); no binary images are committed.

## ADR-008 — Baseline detector: `fsoc_perception`, pixels-only, threshold + CC + weighted centroid
**Status:** Accepted

Step 5 adds the first perception path. Key decisions:

- **Separate `fsoc_perception` library** (`fsoc/detector.hpp` + `src/detector.cpp`), linking
  `fsoc::core` + OpenCV `core`/`imgproc`. It **must not** depend on `fsoc_render`; the
  header includes neither `renderer.hpp` nor `observation.hpp`. Dependency direction is
  `perception -> core + OpenCV` only. The detector consumes `const cv::Mat&` and nothing
  else, so it works on any valid `CV_8UC1` frame — the renderer is just one source.
- **Transparent baseline algorithm** (no CNN / feature / template / filter): threshold
  (`pixel >= threshold_intensity`) -> 8-connected components (OpenCV
  `connectedComponentsWithStats`) -> drop `area < min_bright_pixels` -> pick the component
  with the greatest integrated signal -> intensity-weighted centroid over that component.
- **Connected components (not a single global centroid)** because two bright regions would
  otherwise be averaged together. `connectedComponentsWithStats` also yields the per-
  component area for the size gate. Ties on integrated signal resolve to the lowest label
  (first in raster order) for determinism.
- **Weight `= (pixel − threshold) + 1`.** Subtracting the config threshold (not an assumed
  background — the detector must not encode the renderer's background of 5) removes the
  pedestal so the weighted centroid follows the beacon's true sub-pixel centre; `+1` keeps
  every in-component pixel contributing so `Σ weight ≥ area ≥ 1`. Measured sub-pixel error
  on clean interior frames ≈ 0.02 px (gate 0.15).
- **`BeaconDetection` stays centroid-only** — no fabricated confidence. Diagnostics
  (integrated signal, area) are computable but not added to the controller-facing type.
- **Lost target = `std::nullopt`** (Step-3 contract). Frame validation rejects empty and
  non-`CV_8UC1` inputs with `std::invalid_argument` rather than reinterpreting them.
- CMake `find_package(OpenCV)` components extended to `core imgproc imgcodecs` (no highgui).

## ADR-009 — Pan/tilt PID controller: `fsoc_control`, OpenCV-free, angular-error in / rate out
**Status:** Accepted

Step 6 adds the first control law. Key decisions:

- **Separate `fsoc_control` library** depending on **`fsoc::core` only** (via
  `fsoc/tracking_error.hpp`). OpenCV-free by construction — it links no OpenCV, `fsoc_render`,
  or `fsoc_perception`, and includes no `opencv2/*`. Dependency direction: `control -> core`.
  It builds even when OpenCV is absent (unlike Steps 4/5).
- **Input is `TrackingError.angular` (radians), never pixels.** Controlling on angle makes
  the loop independent of image resolution, focal length, and FOV. `update()` also gets
  `dt_s`; nothing from the truth/perception layers may bypass `TrackingError`.
- **Output is `ControlCommand` = pan/tilt RATE in rad/s**, never absolute angles. The PID
  never touches `PanTiltCamera`; actuation is Step 7. `zero_control_command()` is provided
  for the runner's target-loss path — the PID has no notion of "lost".
- **Standard discrete PID per independent axis:** `u = kp·e + ki·I + kd·D`,
  `I += e·dt`, `D = (e − e_prev)/dt`. **Derivative is forced to 0 on the first update after
  construction/`reset()`** so an undefined previous sample cannot produce a kick. No
  derivative low-pass filtering in the baseline (no demonstrated need yet).
- **Anti-windup = bounded integrator + conditional integration.** `I` is hard-clamped to
  `±integral_limit` every update; additionally, a sample that would only push an
  already-saturated command further into saturation is not accumulated. This keeps `I`
  bounded and lets it unwind immediately once the error reverses — no back-calculation
  needed. Output is clamped to `±output_limit_rad_s`.
- **Sign is preserved, not inverted:** `e > 0` (RIGHT / ABOVE) with `kp > 0` gives
  `command > 0` (PAN RIGHT / TILT UP), matching the frozen `TrackingError` convention and
  `PanTiltCamera::step`.
- **Validation throws before mutating state:** non-finite / `≤ 0` `dt_s` and any non-finite
  `TrackingError` component raise `std::invalid_argument` with the controller state
  untouched; invalid config raises at construction.
- **Default gains are explicitly untuned placeholders** (`kp=1.5, ki=0.2, kd=0.05`,
  `integral_limit=0.5`, `output_limit_rad_s=deg_to_rad(30)`), to be tuned in Step 7. The
  PID never reads `CameraConfig`; the Step-7 runner keeps controller/actuator limits aligned.
- `PIDController::Axis` is a private nested helper, not a public reusable contract.

## ADR-010 — Closed-loop integration: `fsoc_simulation`, pixel-only feedback, P-dominant baseline
**Status:** Accepted

Step 7 wires the tested modules into the first real closed loop. Key decisions:

- **One integration library `fsoc_simulation`** links all four lower libs (`core`, `render`,
  `perception`, `control`). Integration is its job; the lower libs remain unaware of each
  other. `SimulationRunner` owns the clock, the fixed timestep, subsystem call order, the
  target-loss policy, and camera stepping — and duplicates **no** domain math.
- **Frozen `step()` order:** trajectory → `observe_beacon` → `renderer.render` →
  `detector.detect` → `compute_tracking_error` → PID (or loss policy) → `camera.step` →
  record → advance time. The camera is never moved before the frame is computed.
- **Truth vs measurement is enforced, not just intended.** The only feedback path is
  `detector.detect(cv::Mat)` → `compute_tracking_error(detection, camera)` → `pid.update`.
  `TargetState`, `CameraObservation.image_point_px`, and the exact `Projection` populate
  only the diagnostic `SimulationStepResult` fields and the `detection_error_px` score.
  An executable test injects a frame whose detected blob is on the opposite side of centre
  from the true projection and shows the command follows the **detected** blob.
- **Fixed dt only** (0.02 s = 50 Hz), advanced by `sim_time += dt`. No wall-clock in the
  dynamics; the run is bit-for-bit replayable.
- **Target-loss policy:** no detection → `pid.reset()` + zero command + camera holds. No
  search / scan / reacquisition mode in this step. If the target drifts back into the FOV,
  the PID resumes from reset.
- **Trajectory injected by `const Trajectory&`** (caller owns lifetime) — the runner never
  hardcodes a concrete trajectory. The camera is owned by the runner; `PanTiltCamera`
  stays the sole authority on pan/tilt state.
- **`SimulationRunnerConfig::validate()` rejects PID output limit > camera actuator rate**
  and renderer dimensions ≠ camera dimensions, so the runner can never silently demand a
  rate the actuator cannot deliver. The baseline sets PID output limit = camera max rate.
- **Empirically-tuned baseline PID: kp = 12, ki = 0, kd = 0.** Method: the plant (camera
  angle = integral of rate command) already contains one integrator, so with P-only the
  stationary-target loop is first-order (`de/dt = −kp·e`) — non-oscillatory for any
  `kp·dt < 1`. Swept kp ∈ {3, 6, 9, 12, 16, 20}: static settling time 1.44 s → 0.22 s,
  sinusoidal RMS 2.10° → 0.33°, all stable, all 100 % detection. Chose kp = 12: exact
  zero static steady-state error, sinusoidal RMS 0.55°, `kp·dt = 0.24` (well-damped),
  detector-noise amplification ≈ 1.3e-4 rad/s (negligible). Ki = 0: no steady-state bias
  on the integrator plant, and Ki would form a marginally-stable double integrator needing
  Kd. Kd = 0: plant already well damped; derivative would only amplify the ~0.02 px
  detector noise. NOT claimed optimal.
- **`SimulationMetrics` + `evaluate()`** are simple scoring helpers in `fsoc_simulation`,
  not the Step-8 telemetry system (no CSV/JSON/stream/logger).

## ADR-011 — Telemetry is an observer-only sink (`fsoc_telemetry`)
**Status:** Accepted

Step 8 adds telemetry + benchmarking. Key decisions:

- **`fsoc_telemetry` is a SINK.** It links `fsoc::simulation` and consumes
  `SimulationStepResult` values; it never calls back into the runner, PID, camera,
  detector, renderer, or trajectory. A simulation run with telemetry produces a
  bit-identical `SimulationStepResult` sequence to one without it — proven by
  `test_telemetry_non_interference` (run A plain vs run B with a CSV logger interleaved).
- **Absent values are `std::optional`, never a sentinel.** In memory, unavailable
  measurements are `std::nullopt`. In CSV they are empty fields between commas. No `-1`,
  NaN, or `N/A` anywhere in the telemetry API or the documented CSV format.
- **`TrackingState` has two values** (`Tracking`, `TargetLost`). The runner defines no
  deterministic acquisition phase and "flat-out slew while tracking" is already carried by
  `pan_saturated` / `tilt_saturated`, so an `Acquiring` state would be decorative.
- **`*_saturated` = "axis at the actuator rate limit"** (`|command rate| >= max rate - 1e-9`),
  because the baseline sets the PID output limit equal to the actuator rate, which makes
  the literal `|command - applied|` formula always zero. `make_telemetry_record` therefore
  takes the actuator rate limits rather than the record carrying the whole camera config.
- **CSV logger is synchronous** — `std::ofstream`, one line per record flushed to disk, no
  threads / async queue / networking / external CSV library. `column_names()` is the single
  source of column order (and the stable JSON key list for a later frontend).
- **Metrics denominators (documented in `docs/08_TELEMETRY_SCHEMA.md`):** angular/pixel
  error over `tracking_frames`; detection-error over frames with `detection_error_px`;
  saturation and rate stats over all frames.
- **Percentile = nearest-rank**, `index = ceil(0.95 * N) - 1` clamped to `[0, N-1]`, on the
  sorted-ascending magnitudes. No statistics dependency.
- **Wall clock is separate from simulation time.** Physics stays on the fixed `dt = 0.02 s`.
  `processing_fps = frames / wall_execution_time_s` is measured with `std::chrono` around
  the step loop only and never influences the simulation. Reported as a distinct number
  (~4700 FPS / ~90x real time) from the 50 Hz simulation rate.
- `SimulationStepResult` / `SimulationRunner` (Step 7) were NOT modified.

## ADR-012 — Visualization is an observer-only overlay; no runner change (`fsoc_visualization`)
**Status:** Accepted

Step 9 adds the engineering camera-view visualization. Key decisions:

- **`fsoc_visualization` is a SINK.** It links `fsoc::simulation` + `fsoc::telemetry` +
  OpenCV core/imgproc/imgcodecs (videoio iff present). `TrackingVisualizer::annotate()`
  reads a `SimulationStepResult` + `TelemetryRecord` and never calls back into the runner,
  detector, PID, camera, or trajectory. A run with vs without visualization produces a
  bit-identical `SimulationStepResult` sequence (`test_visualization_non_interference`).
- **The perception frame is never contaminated.** `annotate()` takes the `CV_8UC1` frame by
  const reference and does not modify it (byte-for-byte identical after the call). It
  returns a fresh `CV_8UC3` BGR image (`cvtColor` into a new buffer, then draws). The
  detector always runs on the original grayscale frame; overlay pixels cannot reach it.
- **`SimulationRunner` / `SimulationStepResult` were NOT changed.** The base frame for a
  result is reconstructed with `SyntheticCameraRenderer{config.renderer}.render(result.observation)`.
  The renderer is deterministic (Step 4), so this is byte-identical to the frame the runner
  detected on — zero coupling into control, and no heavy `cv::Mat` in the step result or
  telemetry (both stay lightweight). This is the minimal-disturbance path among the options
  in the Step-9 brief.
- **Detection over truth.** The default view shows the detected centroid + error vector.
  The exact projection (`DETECT ERR`, `TRUTH` square marker) is off by default; when enabled
  it is a distinct labelled marker and is never fed into control.
- **Headless first.** PNG per selected frame (`write_png`) is the required path — no
  `cv::imshow` / `cv::waitKey`. MP4 (`try_write_mp4` via `cv::VideoWriter`) is optional and
  best-effort: `videoio` is linked only if the OpenCV build has it (`FSOC_HAS_VIDEOIO`), and
  a missing codec/backend returns `false` without throwing and leaves no partial file. Step
  9 is never RED because MP4 is unavailable.
- Colour semantics are fixed (green = tracking, red = lost, amber = saturation, grey =
  neutral, cyan = optional truth); HUD is degrees for humans while physics stays radians.
- No 3D scene / FOV cone / telemetry graphs here — those belong in the future frontend.

## ADR-013 — Baseline acceptance is an evaluation layer with gates frozen up front (`fsoc_validation`)
**Status:** Accepted

Step 10 adds the baseline acceptance / validation suite — the last gate before a
`v1_baseline` freeze. Key decisions:

- **`fsoc_validation` is an EVALUATION layer, not a component.** It links
  `fsoc::simulation` + `fsoc::telemetry` + `fsoc::visualization`, runs the *existing* v1
  system across named deterministic scenarios, and reads the results. It implements **no**
  trajectory / detector / PID / renderer / camera / pixel→angle math, never enters a
  control path, never advances the loop itself. The only "domain" call it makes is
  constructing a `Trajectory` for a named scenario. Steps 1–9 sources are byte-for-byte
  unchanged (verified with `git diff`).
- **Acceptance thresholds are defined and justified BEFORE evaluation**, in the committed
  `docs/16_BASELINE_ACCEPTANCE.md`, and are **never** derived from the run being scored.
  When a scenario fails, the suite reports it; it does **not** loosen a gate, retry with
  different gains, or tune the controller. The baseline PID stays **kp = 12, ki = 0,
  kd = 0** (ADR-010) — Step 10 changes no algorithm to improve a number.
- **Seven scenarios**, each chosen for a distinct property: A static acquisition (coarse
  alignment — the core product proof), B slow linear (continual tracking), C sinusoidal
  (nonlinear target), D near-FOV-edge acquisition (operational boundary), E actuator
  saturation (correct behaviour *while* rate-limited), F target loss and re-entry
  (explicit lost-target semantics + safe recovery, **not** tracking quality — its low
  detection fraction is expected), G open vs closed loop (proves the controller adds
  value). D and E gate on convergence / final error / limit-compliance, not whole-run RMS,
  because the acquisition transient inflates RMS by design.
- **Global checks on every scenario:** finite values (no NaN/Inf), strictly monotonic
  timestamps, fixed-dt deviation ≤ 1e-9 s, command rate ≤ PID output limit, applied rate ≤
  camera actuator limit, and target-loss semantics (zero command / no `TrackingError` /
  `TargetLost` / empty measurement optionals on every lost frame).
- **Determinism is checked, not assumed.** Each scenario runs twice through independent
  `SimulationRunner`s; the two `BenchmarkMetrics` (excluding wall-clock/FPS) and the two
  `SimulationStepResult` sequences must be bit-identical. The simulation has no RNG.
- **The evaluator is provably able to fail.** `test_failure_check_is_real` (mandatory)
  injects an impossible `AcceptanceCheck` and, separately, tightens a real threshold past
  its measured value, and asserts that `evaluate_passed()` and
  `ValidationSuiteResult::overall_passed` both go `false`. Step 10 is not a decorative
  always-green harness.
- **Evidence uses existing observer paths only.** Per-scenario CSV via the Step-8
  `CsvTelemetryLogger`; annotated PNGs via the Step-9
  `SyntheticCameraRenderer` + `TrackingVisualizer::annotate()` path — no drawing or
  logging code is re-implemented. `generated/step10/VALIDATION_REPORT.md` is generated
  from the run (values never hardcoded). All artifacts land in `generated/step10/`
  (git-ignored); `docs/16_BASELINE_ACCEPTANCE.md` is the one committed Step-10 document.
- **The `v1_baseline` tag was created as a separate step, not by Step 10 itself.** The
  Step‑10 suite passing was the precondition; the tag was then created and pushed to
  `origin` pointing at the merged Step‑10 baseline (`20c028c`). It is **not** moved by
  later work — Step 11 and everything after layer on top of that frozen commit.

## ADR-014 — Demo/frontend packaging is an additive observer layer (`fsoc_demo_support`)
**Status:** Accepted

Step 11 prepares the frozen `v1_baseline` engine for the SIH demo and a future web
frontend without touching a single validated algorithm.

- **`fsoc_demo_support` is a PRESENTATION / PACKAGING layer, not a component.** It links
  `fsoc::simulation` + `fsoc::telemetry` and consumes their outputs. It implements no
  trajectory / detector / PID / renderer / camera / pixel→angle math, never enters a
  control path, and adds no networking, HTTP, WebSocket, or JSON dependency. Geometry,
  camera, trajectory, renderer, detector, `tracking_error`, `pid_controller`, and
  `simulation_runner` are byte-for-byte unchanged (verified by `git diff`). Baseline PID
  stays kp = 12, ki = 0, kd = 0; Step-10 acceptance thresholds are untouched.
- **`DemoScenario` is presentation selection, distinct from `ValidationScenarioId`.** Five
  presets (`static` / `sinusoidal` / `loss` / `open` / `closed`). Their trajectory/config
  parameters are copied verbatim from `src/validation.cpp` via `make_demo_scenario_plan`
  and are never retuned. `OpenLoop` and `ClosedLoop` share byte-identical
  `SinusoidalTrajectory` parameters; only `control_enabled` differs.
- **`DemoSnapshot` is a pure copy / view model.** Built only from `SimulationStepResult` +
  `TelemetryRecord` + `CameraConfig` by `make_demo_snapshot()`. It never feeds control.
  Absent measurements are `std::optional` = `std::nullopt` — no sentinel. The future
  frontend consumes these fields as given and must not recompute PID / tracking error /
  visibility / centroid / camera dynamics (`docs/18_FRONTEND_DATA_CONTRACT.md`).
- **Units boundary is explicit.** Core and `DemoSnapshot` are radians / rad·s⁻¹ / m /
  m·s⁻¹ / px. Degrees exist only in `to_degrees(const DemoSnapshot&)` for the UI. Core
  physics units are not modified.
- **State separation.** `DemoTrackingState` mirrors `fsoc::TrackingState` 1:1
  (control/observation reality). `DemoRunState { Ready, Running, Paused, Finished }` is
  application state. They never mix — `Paused` is not a kind of `TargetLost`, and a paused
  `DemoSession::step()` advances no simulation time (no hidden simulation).
- **Trajectory lifetime.** `DemoSession` declares its `std::unique_ptr<Trajectory>` before
  its `SimulationRunner`, so the heap trajectory outlives the runner that holds it by
  reference; no temporary trajectory is passed.
- **Non-interference is mandatory and tested.** For every scenario, a bare
  `SimulationRunner` and the `DemoSession` produce field-identical `SimulationStepResult`
  sequences. `fsoc_step11_tests` (23 checks) also re-runs the Step-10 `ValidationSuite` and
  asserts it still passes 7/7.
- **The CLI reuses existing code.** `fsoc_demo` computes its summary with the Step-8
  `compute_benchmark_metrics`; no metrics math is duplicated. `make demo` /
  `scripts/run_baseline_demo.sh` is the reproducible bundle (`set -euo pipefail`, no
  destructive git ops, no hardcoded paths).

## ADR-015 — V2 AI PERCEPTION: a learned detector behind the frozen `BeaconDetection` contract
**Status:** Accepted (phase in progress on `feat/ai-perception`; `v1_baseline` untouched)

The validated v1 pipeline (`threshold → connected components → brightest component →
intensity-weighted centroid`) is transparent and sub-pixel-accurate on clean Gaussian
beacons, and it stays the frozen baseline. Its real weakness is *selection*: it has no
model of which bright region is the beacon, so under low SNR, star clutter, hot pixels,
blur/defocus, background gradients, or a distractor brighter than the target it locks onto
the wrong blob or fabricates a detection. V2 adds a learned perception stack that is robust
in those regimes, **without touching the controller**.

- **Scope is perception only.** The AI produces the *same* `std::optional<BeaconDetection>`
  the classical detector produces. `compute_tracking_error`, the PID law, the baseline
  gains (kp = 12, ki = 0, kd = 0), `PanTiltCamera`, the actuator limits, the frozen
  `SimulationRunner::step()` order, and the Step-10 acceptance thresholds are all unchanged.
  No UKF / MPC / temporal model / trajectory prediction in this phase (those are ADR-0xx
  future work, documented in `docs/19`).
- **Not YOLO.** The target is a sub-pixel point source; bounding-box detection is the wrong
  abstraction. `TinyBeaconNet` is a lightweight fully-convolutional **heatmap-localization**
  network: single-channel input → (presence logit, location heatmap) → soft-argmax
  sub-pixel centroid. Design budget < 1M params (actual ≈ 50k); real-time CPU inference.
- **Pixels only at inference.** The learned detector receives a `cv::Mat` and nothing else
  — never `TargetState`, trajectory, projected truth, `TrackingError`, or controller state.
  Ground-truth labels (`target_present`, `x_px`, `y_px`) exist only in dataset generation,
  training, and evaluation.
- **Runtime stays C++.** Python (PyTorch) is permitted *offline only* for dataset tooling
  and training, reversing ADR-001's Python ban for that narrow purpose (AI brief §5). The
  trained model is exported to **ONNX** and run in C++ through **OpenCV 5 `dnn`** (already
  present in the toolchain). No Python runtime, server, or glue in the closed loop / demo.
- **Additive build + files only.** New libraries `fsoc_ai_datagen` (synthetic frame
  synthesizer for datasets + AI eval), later `fsoc_ai_perception` (ONNX detector + hybrid)
  and `fsoc_ai_validation` (new eval suite). New app `generate_ai_dataset`. `fsoc_core`
  stays OpenCV-free; `fsoc_ai_datagen` links `fsoc::core` + OpenCV core/imgproc and — like
  `fsoc_perception` — must not depend on `fsoc_render`. Datasets and AI eval artifacts are
  git-ignored under `generated/`; the small `models/tiny_beacon_net.onnx` is committed.
- **Synthetic-domain limitation is stated, not hidden.** The first model is trained purely
  on domain-randomized virtual-camera imagery. No real sensor data, no atmospheric dataset,
  no flight heritage — `docs/19` and `models/MODEL_CARD.md` say so explicitly.

## ADR-016 — Perception is made pluggable via an additive strategy seam, default = Classical
**Status:** Accepted (integration lands in a later stage of this phase)

The AI must participate in real closed-loop tracking, not just a static screenshot demo, so
`SimulationRunner` needs to be able to run classical / AI / hybrid perception. Two options
were considered:

1. **Fork a post-v1 runner** that copies the loop and swaps the detector. Rejected: it
   duplicates the frozen `step()` ordering and target-loss policy, and any future baseline
   fix would have to be mirrored in two places — exactly the "god-loop / copy-paste"
   failure the architecture forbids.
2. **Additive strategy seam in the existing runner** (chosen). Introduce
   `enum class PerceptionMode { Classical, AI, Hybrid }` and a narrow
   `PerceptionStrategy` interface whose single job is `cv::Mat → std::optional<BeaconDetection>`
   (+ diagnostics). `SimulationRunnerConfig` gains a `PerceptionMode` field that **defaults
   to `Classical`**; in that mode the runner calls the existing `BeaconDetector` exactly as
   today. `step()` order, the clock, the loss policy, and camera stepping are unchanged.

Guarantees for option 2:
- **Bit-identical default.** A regression test runs every Step-10 / demo scenario through
  the seam in `Classical` mode and asserts a field-identical `SimulationStepResult`
  sequence versus a bare v1 `SimulationRunner`. Step-10 must still end
  `STEP 10 BASELINE ACCEPTANCE: PASS`.
- **No truth to detectors.** The strategy is handed only the `cv::Mat`; the seam sits at
  the exact point the runner already calls `detector_.detect(frame)`.
- **Diagnostics stay out of control.** AI confidence / perception source / inference-ms /
  classical-vs-AI distance are recorded as telemetry-only fields (`PerceptionSource` is a
  diagnostic enum, never a `TrackingState` / `DemoRunState`). The controller never reads
  neural confidence.
- **Hybrid policy is config-driven and documented** (agreement radius, AI confidence
  threshold, high-confidence override) — see `docs/19_AI_PERCEPTION_ARCHITECTURE.md`.

**Note (ADR-018).** The confidence-override escape hatch sketched above — AI confidence
resolving classical/AI disagreement, or gating an AI-only detection — was retired after
Stage-2 training evidence showed confidence does not separate correct from wrong accepted
detections. See ADR-018 for the Safe Hybrid policy that replaces it. This ADR is kept
as-written for history; it is not silently rewritten.

## ADR-017 — Stage-2 training fix: foreground-weighted heatmap loss + max-pool presence head
**Status:** Accepted (Stage 2 of `feat/ai-perception`; offline training toolchain only — no
C++ runtime, no `v1_baseline`, no closed-loop code touched)

The first real training run of `TinyBeaconNet` (the Stage-1 config: plain
`MSE(sigmoid(heatmap_logit), gaussian_heatmap)` with `λ_h = 1`, and a global-**average**-pool
presence head) **stalled**: after ~4 epochs the presence head sat at false-positive rate
≈ 0.4–0.5 and the centroid MAE plateaued at ~40–45 px. A `git`-clean diagnostic (kept in the
Stage-2 evidence, not committed) established:

- **Pipeline is correct.** On a fixed 96-sample subset the presence head reaches 100 %
  accuracy and the median centroid error is ~2 px — labels, coordinate maps (`common.py`
  round-trip is exact), `/255` normalization, output shapes and gradient flow are all fine.
- **Root cause 1 — the heatmap loss has almost no gradient.** The 60×80 target grid is
  ~97 % background (the σ = 1.75-cell Gaussian covers ~120 of 4800 cells). Unweighted MSE on
  the sigmoid surface is minimised by predicting a near-flat ~0 map (MSE ≈ 1.4e-3), and the
  handful of foreground cells cannot pull the surface into a peak. `docs/20 §7`'s assumption
  "at 60×80 there is no severe dense class imbalance" was wrong. Raising `λ_h` alone does
  not help (it scales an already-flat gradient). **Fix:** a foreground-weighted MSE — each
  cell with `target > 0` gets weight `1 + pos_weight` (`pos_weight = 80` default, new
  `--heatmap-pos-weight` CLI flag; `0` recovers the old loss). On the probe this drops
  centroid MAE 42 → 15 px and p90 182 → 6 px.
- **Root cause 2 — global-average pool washes out a point source.** The beacon occupies
  << 1 cell of the trunk feature map; averaging over ~4800 cells destroys the "is there a
  peak anywhere" signal the presence head needs. **Fix:** `nn.AdaptiveAvgPool2d(1)` →
  `nn.AdaptiveMaxPool2d(1)` in `presence_head`. Combined with the weighted loss the probe
  reaches MAE 12 px / p90 4.6 px / 4 % of positives > 10 px, presence 100 %.
- **Rejected:** CornerNet-style penalty-reduced pixel BCE on the soft heatmap — it broke
  the presence head on the probe (acc 0.82, a negative at prob 0.92). Not worth the extra
  loss complexity when weighted MSE works.

Scope of the change: `tools/ai/model.py` (pool swap — ONNX I/O contract unchanged: input
`[N,1,240,320]`, outputs `[N,1]` + `[N,1,60,80]`, opset 12; `MaxPool` is OpenCV-DNN-safe),
`tools/ai/train_beacon_net.py` (weighted loss + flag; checkpoint criterion is now
`min val_total = λ_p·BCE(presence) + λ_h·weightedMSE(heatmap)` — the training objective),
`tools/ai/export_onnx.py` (`dynamo=False` pins the legacy opset-12 exporter under
torch ≥ 2.9), and the docs below. `docs/19 §2`, `docs/20 §7/§9` updated in the same change.
A runnable regression, `tools/ai/selfcheck.py`, asserts a short run drives presence
accuracy ≥ 0.95 and centroid median ≤ 5 px / p90 ≤ 15 px on a real subset. The dataset,
the frozen `common.py` numeric contract, and every C++ module are untouched.

**Coda — Stage-2 training ceiling (kept, not worked around).** The ADR-017 fixes let
`TinyBeaconNet` learn a real beacon-PSF signature: with the calibrated safe threshold
(0.95, val-only, `docs/20 §10`) the model localizes the beacons it commits to at
**median 1.7 px (97.5 % ≤ 10 px)** with a ~1 % false-lock rate on the untouched test
split — including under star clutter, a brighter distractor, and blur. But its **recall
is ~40 %**: the presence head cannot cleanly separate "beacon present" from
"clutter only" (val ROC-AUC ≈ 0.82), and the model deliberately **abstains** on
low-SNR / dim / edge-clipped beacons rather than lock onto the wrong blob. Ten
documented training variants — plain/weighted MSE, weighted/plain BCE and a
0.5·MSE+0.5·BCE heatmap loss; channel width 16 / 24 / 32; stem stride 1 vs 2 and a
double-conv stem; a global-max context head (`torch.amax` + 1×1 conv + concat) —
**all plateau at the same point** (frac ≤ 10 px ≈ 0.74 over all positives, presence
AUC ≈ 0.82). A ~27 k-param single-frame CNN cannot reliably out-select a distractor
rendered brighter than the target with an overlapping σ range (30 % of positives, by
dataset design — `docs/20 §3`, the intended hard case). This is **reported, not tuned
around**: recall is expected from the Stage-3 hybrid policy (the classical detector)
and, longer-term, the Phase-2 spatio-temporal detector (`docs/19 §8`). No easier test
set was generated; the frozen threshold was chosen on val before test was scored.

## ADR-018 — Post-Stage-2 safety revision: AI is candidate perception, not independent control authority
**Status:** Accepted (documentation / architecture-decision only — Stage 3 implementation has
**not** started; `v1_baseline`, the PID, Step-10, and the trained model are untouched)

ADR-016 fixed the perception *seam* (additive `PerceptionMode`, default `Classical`,
bit-identical regression) but left the Hybrid arbitration policy provisional, including a
"high-confidence AI can override classical / resolve disagreement" escape hatch pending real
training results. Stage-2 training + evaluation (`docs/20 §10`, `models/stage2_test_report.json`)
now supplies those results, and they invalidate that escape hatch.

**Original assumption (ADR-016 draft policy).** High AI presence confidence could be used to
trust an AI-only detection, or to let AI override the classical centroid on disagreement
(`confidence ≥ high_confidence_threshold`, draft default 0.90).

**Stage-2 evidence that disproves it** (frozen checkpoint epoch 9, frozen presence threshold
0.95, untouched test split, n = 1200):
- confidence does **not** separate correct from incorrect *accepted* detections — mean peak
  confidence on correct (≤ 10 px) detections ≈ **0.972**, on wrong (> 25 px) detections ≈
  **0.965**: materially overlapping distributions, not separable by any confidence cut;
- one accepted detection at presence probability ≈ **0.999** ("very high confidence") has a
  centroid error of ≈ **630.99 px** — a wrong-blob lock, not a near-miss (`test_000821`,
  `generated/ai_stage2/evidence/heatmaps/FAILURE_wrong_blob__test_000821.png`);
- standalone AI recall at the frozen safe threshold is only ≈ **40.44 %** (test precision
  98.91 %, FPR 1.33 %) — the model itself already withholds ~60 % of judgements as unreliable
  rather than guess; accepting an *unconfirmed* AI-only candidate would undo that caution;
- when AI **does** agree with an independently-derived candidate, localization is excellent
  (median ≈ **1.71 px**, P95 ≈ **5.66 px**) — the network's geometry/precision is not in
  question, only its unaccompanied identity judgement.

**Decision — the Safe Hybrid policy** (full table: `docs/19 §5`):

1. **Classical + AI agree** (≤ `agreement_radius_px`) → **accept**; control-facing centroid =
   **classical** (superior clean sub-pixel precision); diagnostic source `HybridAgreement`.
2. **Classical only** → **accept classical**; diagnostic source `Classical`.
3. **AI only** → **no control authority.** The AI candidate (centroid, confidence) is kept as
   diagnostic telemetry only; control-facing result is `std::nullopt`; diagnostic rejection
   reason `AiOnlyUnverified`. Not because the candidate is necessarily wrong — because a single
   frame gives no way to confirm it is right.
4. **Classical + AI disagree** (> `agreement_radius_px`) → **reject, unconditionally.** No
   averaging, no brightness tiebreak, no confidence override — the `high_confidence_threshold`
   escape hatch from the ADR-016 draft is **retired**. Control-facing result `std::nullopt`;
   diagnostic rejection reason `DetectorDisagreement`. A momentary `TargetLost` is strictly
   safer than a ~600 px commanded slew toward the wrong optical source.
5. **Neither detects** → `std::nullopt`; diagnostic source `None` (unchanged from ADR-016).

**Agreement radius.** `agreement_radius_px` is frozen at an initial Stage-3 engineering default
of **8.0 px** — explicitly **not** an ML confidence threshold, but the source-space size of one
heatmap cell (`INPUT_STRIDE = 8`, confirmed against `tools/ai/common.py` — 640/80 = 480/60 = 8),
sized to absorb ordinary heatmap-grid quantization and the model's own accepted-detection error
(median 1.71 px, P95 5.66 px) without letting a large disagreement through. Not to be tuned
against future Stage-4 closed-loop results.

**Consequence.** Under `PerceptionMode::Hybrid`, `PerceptionSource::AI` is **never emitted** —
AI never independently supplies the control-facing centroid. `PerceptionMode::AI` (explicit,
non-default, diagnostic/benchmark mode) is **unaffected** by this ADR: there, the thresholded
AI candidate may be exposed as the detector's own output for evaluation, because that mode's
job is to characterise the network, not to command the gimbal. Default production/demo
perception remains `Classical` (ADR-016) until further validation says otherwise. Stage 3
prioritizes safety and explainability over maximizing AI control authority.

**Deferred, not abandoned — the future temporal gate.** AI-only reacquisition may be safely
reconsidered once a runtime motion-consistency gate exists: previous accepted track history +
current AI candidate + a consistency check, decided from **runtime observation history only**
— never `TargetState` truth, the exact simulated `Projection`, trajectory truth, future target
position, or any diagnostic truth-error field (the ADR-004 ground-truth boundary applies to
this gate exactly as it does to the classical/AI detectors). This is the proper mechanism for
resolving single-frame target-identity ambiguity — documented in `docs/19 §9` for a later
phase; **not implemented, not scheduled as Stage 3.**

**Scope of this ADR.** Documentation / architecture-decision only. No C++ written, no Python
training code touched, no model retrained, `v1_baseline` / PID / Step-10 gates untouched. Stage
3 has not started.

## ADR-019 — P0-v2: minimal state estimator implements the ADR-018 §9 temporal gate; Hybrid V2 = Hybrid + tracker

**Status:** Accepted and implemented (`fsoc::TargetTracker`, `include/fsoc/target_tracker.hpp`),
additive and default-off (`tracker_enabled = false`); `v1_baseline`, PID gains, the classical
detector algorithm, and `resolve_perception()` itself are all untouched.

ADR-018 §9 (`docs/19 §9`) documented — but explicitly deferred — a future runtime
motion-consistency gate: "previous accepted track + current candidate + a consistency check,
decided from runtime observation history only." The MVP-V2 completion pass required a minimal
state estimator (Phase B) and, separately, a fix for the clutter false-lock weakness Stage-4
measured (`docs/MVP_METRICS.md §2`: 44.9% common-frame FPR, 2,240 severe closed-loop outliers
`>50px` for Classical). Building the estimator as an alpha-beta (g-h) filter with a temporal
outlier gate (`TargetTracker::update`, `outlier_gate_px`) directly IS the ADR-018 §9 gate — this
ADR records that the prerequisite it named now exists, tested, and measured.

**What was NOT changed.** `resolve_perception()` (ADR-018's fusion policy: agreement / classical
/ AI-only-rejected / disagreement-rejected / none) is byte-for-byte unchanged. The classical
detector algorithm is unchanged. The tracker is a NEW, separate, purely additive stage layered
**after** `resolve_perception()` — exactly the seam ADR-016 established for `PerceptionMode`
itself: default off, bit-identical when disabled (regression-tested in both
`tests/step7_tests.cpp` and `tests/stage4_determinism_tests.cpp`).

**Root cause found (not assumed).** An early version of this gate, evaluated empirically via
the new `stage4_tracker_ablation` tool (`docs/MVP_ABLATION.md`), showed the tracker could make
outcomes *worse* on Stage-4's `BrightDistractor` scenario: `TargetTracker`'s original acquisition
logic confirmed `Acquiring -> Tracking` after N consecutive measurements regardless of whether
they agreed with each other. Because Stage-4's distractor is placed at a uniformly random
position, independent of the beacon, and re-rolled every frame (`stage4_degradation.cpp
apply_clutter`), a bad acquisition could confirm a stable lock on 3 mutually inconsistent
clutter positions just as readily as 3 consistent real-beacon ones — and once "confirmed," the
established-track outlier gate then rejected the *real* beacon as the outlier. Fix: acquisition
now also checks each new candidate against `outlier_gate_px`; an inconsistent candidate restarts
acquisition fresh rather than counting toward confirmation (`TargetTracker::begin_acquisition`).

**Hybrid V2 — the combined policy.** "Hybrid V2" is not a new `PerceptionMode` value (ADR-018's
enum stays frozen) — it is `PerceptionMode::Hybrid` with the tracker enabled downstream
(`SimulationRunnerConfig::tracker_enabled = true`, or the equivalent optional parameter on
`stage4::run_closed_loop_scenario_mode_seed`). Measured over the full frozen Stage-4 closed-loop
protocol (11 scenarios x 5 seeds x 8s @ 50Hz, `docs/MVP_ABLATION.md`): severe (`>50px`) control
outliers fall from 1,808 (Hybrid) to 9 (Hybrid+Tracker) — a 99.5% reduction — at a coverage cost
of roughly 20 percentage points (97.35% -> 77.05% accepted-detection fraction), concentrated
almost entirely in the two adversarial-clutter scenarios; the 9 non-adversarial scenarios lose
only the ~0.5-point acquisition-ramp cost with zero change in outlier counts.

**Deferred, still not implemented — AI-only reacquisition.** ADR-018 case 3 (AI-only candidate,
no control authority) is UNCHANGED by this ADR. The gate built here filters whatever
`resolve_perception()` already outputs; it does not yet let a tracker-consistent AI-only
candidate through `resolve_perception()` itself when Classical produces nothing. That specific
extension — described precisely in `docs/19 §9` — is now technically unblocked (the prerequisite
gate exists and is tested) but remains a distinct, unimplemented change to the frozen fusion
policy, out of scope for this ADR.

**Scope of this ADR.** `include/fsoc/target_tracker.hpp` / `src/target_tracker.cpp` (new,
additive module), `SimulationRunner`/`stage4_closed_loop_bench` wiring (both additive, default
off, bit-identical-when-disabled, regression-tested), `apps/stage4_tracker_ablation.cpp` (new
evaluation tool). `resolve_perception()`, the classical detector, PID gains, and `v1_baseline`
are untouched.
