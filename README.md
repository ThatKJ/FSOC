# SIH26169 — FSOC Virtual Camera Tracking MVP (C++20)

Engineering starter kit for **AI-Based Virtual Camera Tracking System for Coarse Alignment of Mobile Free Space Optical Communication (FSOC) Terminals**.

This repository treats the challenge as a **closed-loop guidance, tracking, and control problem**:

`Environment -> Camera -> Beacon measurement -> Estimation -> Prediction -> Control -> Pan/Tilt actuation -> Observation`

## Language decision

The project baseline is now **modern C++20**. There is no Python package, virtual environment, pip install, `pyproject.toml`, NumPy, or PyVista dependency in the core project — i.e. in the **runtime control loop**. `tools/ai/` is a separate, offline, one-time model-training toolchain (PyTorch → ONNX export, `tools/ai/README.md`) that produces the committed `models/tiny_beacon_net.onnx`; it is never imported, run, or required by the C++ runtime, which loads that ONNX file through OpenCV-DNN (see "AI Perception" below).

For the first 48-hour MVP:
- Core math/physics/control: C++20
- Build: CMake + Ninja
- Pixel simulation/tracking visualization: OpenCV C++ (introduced after the math gate)
- Step-1 vector math: dependency-free to keep the foundation auditable
- Later UKF/MPC phase: add Eigen when matrix-heavy estimation/control begins

## Step 1 already implemented

- 3D world convention
- pan/tilt camera basis
- pinhole projection
- finite camera FOV
- actuator velocity saturation
- tilt mechanical limits
- ideal pointing angles for diagnostics only
- terminal-only smoke test
- 12 unit checks using CTest, with no external test framework

## Step 2 implemented — target trajectory engine

- `TargetState` = world `position_m` + `velocity_mps` (SI, double precision)
- `Trajectory` abstract interface: pure `state_at(double time_s)`, no owned clock
- stationary, linear constant-velocity (signed), and sinusoidal trajectories
- sinusoidal velocity is the exact analytic derivative; frequency in Hz (`omega = 2*pi*f`)
- deliberate input validation via `std::invalid_argument` (non-finite / negative time,
  non-finite params, negative frequency/amplitude)
- `step2_trajectory_smoke` + deterministic analytic CTest suite (`fsoc_step2_tests`)
- no coupling to camera / perception / control

## Step 3 implemented — observation / measurement / tracking-error contracts

- strongly typed layers kept distinct: `TargetState` (truth) → `CameraObservation` /
  `Projection` (exact projection) → `BeaconDetection` (image estimate) → `TrackingError`
  (controller-facing)
- `ObservationStatus` = `Visible` / `OutsideFieldOfView` / `BehindCamera`;
  `observe_beacon()` reuses `PanTiltCamera::project()` — no duplicated projection math
- frozen image convention: origin top-left, `+x_px` right, `+y_px` down;
  centre `cx = W/2.0`, `cy = H/2.0` owned by the camera
- frozen sign convention: pixel `error_x>0` = RIGHT, `error_y>0` = BELOW;
  angular `pan_rad>0` = command pan right, `tilt_rad>0` = command tilt up
- `compute_tracking_error(std::optional<BeaconDetection>, PanTiltCamera)` — `optional`
  in / out, non-finite centroid rejected, reuses `pixel_error_to_angles`
- target-lost = empty `std::optional` only (no `(-1,-1)` / NaN / zero sentinels)
- `step3_observation_smoke` (machine-checks the critical (400,180) scenario) +
  `fsoc_step3_tests` (all four quadrants, pinhole match, regressions)
- no OpenCV, no detector algorithm, no controller

## Step 4 implemented — synthetic virtual-camera image renderer

- first OpenCV use, isolated in a separate `fsoc_render` library; `fsoc_core` and all
  pure-math headers stay OpenCV-free
- `SyntheticCameraRenderer::render(const CameraObservation&) -> cv::Mat` (`CV_8UC1`)
- uniform dark background (default 5 counts) + analytic 2-D Gaussian beacon
  (peak 255, `sigma` in **pixels**), clamped to `[0,255]`
- **true sub-pixel** beacon centre — the fractional `ImagePoint` is never rounded before
  the Gaussian is evaluated, so a weighted centroid recovers it
- edge-safe: the Gaussian is rasterised in a window clipped to the image
- `OutsideFieldOfView` / `BehindCamera` → background-only frame (no fake beacon)
- no sensor noise this step; the same observation renders byte-identical frames
- `step4_renderer_smoke` writes `generated/*.png` headlessly + `fsoc_step4_tests`
- CMake `find_package(OpenCV)` auto-detected (`FSOC_ENABLE_OPENCV=AUTO|ON|OFF`)

## Step 5 implemented — baseline beacon detector

- new `fsoc_perception` library (`fsoc::core` + OpenCV core/imgproc); **does not depend on
  `fsoc_render`** — `BeaconDetector::detect(const cv::Mat&)` consumes pixels only, never
  `TargetState` / trajectory / `CameraObservation` / the projected `ImagePoint`
- transparent pipeline: threshold (`pixel >= threshold_intensity`, default 64) →
  8-connected components → reject `area < min_bright_pixels` → pick the component with the
  greatest integrated signal (ties: lowest label) → intensity-weighted centroid
- centroid weight `= (pixel − threshold) + 1` (no assumed background); recovers the
  Gaussian's sub-pixel centre to **≈ 0.02 px** on clean interior frames (gate 0.15)
- found → `std::optional<BeaconDetection>` (Step-3 type); not found → `std::nullopt`
  (no `(-1,-1)` / NaN / zero sentinel); no fabricated confidence
- rejects empty / non-`CV_8UC1` frames with `std::invalid_argument`
- perception chain verified: renderer → detector → `compute_tracking_error` reproduces
  RIGHT+ABOVE → pan > 0, tilt > 0
- `step5_detector_smoke` (headless, `std::chrono` timing for curiosity) + `fsoc_step5_tests`

## Step 6 implemented — pan/tilt PID controller

- new `fsoc_control` library depending on **`fsoc::core` only** (via `fsoc/tracking_error.hpp`);
  **OpenCV-free** — links no OpenCV / `fsoc_render` / `fsoc_perception`, builds without OpenCV
- `PIDController::update(const TrackingError&, double dt_s) -> ControlCommand` — angular
  error (radians) in, pan/tilt **rate** (rad/s) out; never absolute angles, never touches
  `PanTiltCamera`
- two independent axes, standard discrete PID `u = kp·e + ki·I + kd·D` on `angular.pan_rad`
  / `angular.tilt_rad`; derivative forced to 0 on the first update after construction/`reset()`
- anti-windup: integral hard-clamped to ±`integral_limit` + conditional integration; output
  clamped to ±`output_limit_rad_s`
- `reset()` clears integrals / previous errors / first-sample flags; `zero_control_command()`
  helper for the runner's target-loss path
- rejects non-finite / ≤0 `dt_s` and non-finite `TrackingError` with `std::invalid_argument`
  (state untouched on throw); invalid config rejected at construction
- sign preserved: `e > 0` (RIGHT / ABOVE) → command > 0 (PAN RIGHT / TILT UP)
- default gains are **untuned placeholders** (tuned in Step 7)
- `step6_pid_smoke` (5 scenarios + toy scalar-plant sanity) + `fsoc_step6_tests`

## Step 7 implemented — closed-loop tracking simulation

- new `fsoc_simulation` library (links `fsoc::core` + `render` + `perception` + `control`)
  — the **one** intentional integration layer; owns the clock, fixed timestep, subsystem
  call order, target-loss policy, and camera stepping, and **no** domain math
- `SimulationRunner::step()` runs one fixed timestep in this order: `trajectory.state_at(t)`
  → `observe_beacon` → `renderer.render` → `detector.detect(cv::Mat)` →
  `compute_tracking_error(detection, camera)` → `pid.update` (or loss policy) →
  `camera.step` → record `SimulationStepResult` → `t += dt`
- fixed `dt = 0.02 s` (50 Hz); **never wall-clock**; same config + trajectory →
  bit-identical result sequence
- **pixel-only feedback:** control is driven solely by the detected centroid;
  `TargetState` / `observation.image_point_px` / exact `Projection` feed only the labelled
  diagnostic fields and truth-vs-measurement scoring — proven by
  `test_control_follows_detected_not_truth`
- **target-loss policy:** no detection → `pid.reset()` + zero command + camera holds (no
  search); the loop resumes from reset if the target drifts back into the FOV
- `SimulationRunnerConfig::validate()` rejects PID output limit > camera actuator rate and
  renderer/camera dimension mismatch
- empirically-tuned MVP baseline PID **kp = 12, ki = 0, kd = 0** (P-dominant on the
  integrator plant — not claimed optimal); results: static acquisition **4.13° → 0.0° in
  ~0.34 s**, sinusoidal (±12.4°) RMS **0.55°** at 100 % detection, open-loop → closed-loop
  detection **57 % → 100 %** and RMS **6.45° → 0.55°**
- `step7_closed_loop_smoke` (static / sinusoidal / open-vs-closed) + `fsoc_step7_tests`

## Step 8 implemented — telemetry + benchmarking

- new `fsoc_telemetry` library — an **observer**: consumes `SimulationStepResult`, never
  calls back into the loop. Running with vs without telemetry yields a bit-identical
  `SimulationStepResult` sequence (mandatory non-interference test)
- `TelemetryRecord` — 27 flat, unit-suffixed, JSON-mappable fields; unavailable
  measurements are `std::optional` in memory (**no `-1` / NaN / `N/A` sentinel**) and empty
  fields in CSV; `TrackingState { Tracking, TargetLost }`
- `CsvTelemetryLogger` — synchronous `std::ofstream`, one flushed line per record, no
  threads / async / external CSV dependency; writes `generated/step8_*.csv` (git-ignored)
- `BenchmarkMetrics` / `compute_benchmark_metrics` — detection %, RMS/mean/max/final/**P95**
  angular error, mean/RMS/max pixel error, mean detection error, command/pan/tilt
  saturation fractions, mean|abs|+peak applied rates, wall time + processing FPS. Error
  metrics over frames with a `TrackingError`; percentile = nearest-rank `ceil(0.95·N)-1`
- **wall clock vs simulation clock:** physics stays on the fixed `dt = 0.02 s` (50 Hz);
  `processing_fps = frames / wall_time` is measured with `std::chrono` around the step loop
  only (~4700 FPS ≈ 90× real time) and never feeds the sim dt
- `step8_telemetry_smoke` runs the 4 benchmark scenarios, exports CSVs, prints the
  comparison table (Static P95 0.17°, Sinusoidal-closed P95 0.79° vs Sinusoidal-open P95
  9.81°) + `fsoc_step8_tests`

## Step 9 implemented — engineering camera-view visualization

- new `fsoc_visualization` library — an **observer**: `TrackingVisualizer::annotate()` takes
  the perception `CV_8UC1` frame **by const& (never modified)** and returns a **new
  `CV_8UC3` BGR** display frame. The control path keeps running on the original unannotated
  image; overlay pixels can never reach the detector
- `SimulationRunner` / `SimulationStepResult` **not changed** — the base frame is
  reconstructed from `result.observation` via a deterministic `SyntheticCameraRenderer`
- overlays: centre crosshair (from frame geometry, not hardcoded 320/240), detection marker
  at `telemetry.detected_*`, centre→detected error vector (shrinks to zero on convergence),
  `TRACKING` / `TARGET LOST`, `VISIBLE` vs `DETECTED`, SIM/FRAME, PAN/TILT (deg), ANG ERR
  (deg), ERR PX, CMD rates (deg/s) with amber `RATE LIMIT` from the Step-8 `*_saturated`
  flags
- colours: green = tracking, red = lost, amber = saturation, grey = neutral; optional
  `DETECT ERR` and `TRUTH` square marker are **off by default**
- headless: PNG per selected frame (required) + optional best-effort `cv::VideoWriter` MP4
  (graceful `false` when no codec / no `videoio`); output → `generated/step9/` (git-ignored)
- mandatory non-interference test passed (500 frames with/without annotation → identical
  `SimulationStepResult` sequence); static story visually verified (frame 0: 4.13° / long
  vector / 30°/s + `RATE LIMIT` → final: beacon on crosshair / 0.00° / 0°/s)
- `step9_visualization_smoke` (static / sinusoidal / target-lost) + `fsoc_step9_tests`

## Step 10 implemented — baseline acceptance / validation suite

- new `fsoc_validation` library (links `fsoc::simulation` + `fsoc::telemetry` +
  `fsoc::visualization`) — an **evaluation layer**. It runs the *existing* v1 system across
  seven named deterministic scenarios and checks acceptance gates; it implements **no**
  trajectory / detector / PID / renderer / camera math and never controls the loop or
  changes an algorithm to improve a number
- **gates are frozen up front** in `docs/16_BASELINE_ACCEPTANCE.md` — physically justified,
  documented, **not** derived from the run being scored. Baseline PID stays **kp = 12,
  ki = 0, kd = 0**
- scenarios: **A** Static Acquisition · **B** Slow Linear Tracking · **C** Sinusoidal
  Tracking · **D** Near-FOV-Edge Acquisition · **E** Actuator Saturation · **F** Target
  Loss and Re-entry · **G** Open Loop vs Closed Loop
- per-scenario global checks: finite values (no NaN/Inf), monotonic timestamps, fixed dt,
  command rate ≤ PID limit, applied rate ≤ actuator limit, target-loss semantics, and a
  **deterministic-replay** check (a second independent run is bit-identical)
- a **mandatory failure-check test** injects an impossible gate and tightens a real
  threshold past its actual value and confirms the evaluator then reports FAIL — it is not
  an always-green harness
- `step10_validation_smoke` prints a judge-friendly table, writes CSV + annotated PNG
  evidence and `generated/step10/VALIDATION_REPORT.md` (values generated from the run, not
  hardcoded), and ends with `STEP 10 BASELINE ACCEPTANCE: PASS` — **7 / 7 scenarios pass**
  (static 4.13° → 0.00°; sinusoidal RMS 0.55°; open→closed detection 57.4 % → 100 %, RMS
  6.45° → 0.55°, ×11.8). `fsoc_step10_tests` green (10 checks)
- generated evidence → `generated/step10/` (git-ignored); the canonical gate definitions
  live in `docs/16_BASELINE_ACCEPTANCE.md` (committed). The **`v1_baseline` tag is created
  and pushed** to `origin`, pointing at the merged Step‑10 baseline (`20c028c`); that
  validated baseline is **frozen**

## Step 11 implemented — demo freeze + frontend data contract prep

- new `fsoc_demo_support` library (links `fsoc::simulation` + `fsoc::telemetry`) — an
  **additive** presentation layer built *after* the frozen baseline. `v1_baseline` (tag,
  pushed to `origin`) stays put; this step changes **no** validated algorithm. Geometry /
  camera / trajectory / renderer / detector / `TrackingError` / PID law / PID gains /
  `SimulationRunner` order / Step-10 gates are all untouched.
- **`DemoScenario`** — `static` · `sinusoidal` · `loss` · `open` · `closed`, selected by a
  clean token (`parse_demo_scenario`). Each reuses the validated Step-10 trajectory/config
  **verbatim** (no retuning). `open` and `closed` share identical trajectory parameters —
  only `control_enabled` differs.
- **`DemoSnapshot`** — a per-frame view model for a future UI, built **only** from
  `SimulationStepResult` + `TelemetryRecord` + `CameraConfig` by `make_demo_snapshot()`. It
  never participates in control. Optionals are `std::nullopt` when the target is lost — no
  sentinels. Fields + the future JSON shape are frozen in
  `docs/18_FRONTEND_DATA_CONTRACT.md`.
- **Units** — core stays radians / rad·s⁻¹ / m / m·s⁻¹ / px. Degrees appear only via
  `to_degrees(const DemoSnapshot&)` at the UI boundary; core physics units are unchanged.
- **`DemoSession`** — deterministic packaging of one scenario: owns a heap `Trajectory`
  (constructed before, so it outlives the `SimulationRunner`) + the runner + the telemetry
  conversion. `DemoRunState { Ready, Running, Paused, Finished }` is application state,
  distinct from `TrackingState`; a **paused `step()` does not advance simulation time**.
  `reset()` reproduces a bit-identical run.
- **`fsoc_demo` CLI** — `./build/debug/fsoc_demo <scenario> [--duration s] [--csv path]
  [--quiet]` / `--help`. Per-frame status lines + an end-of-run summary (detection %, RMS /
  P95 / max angular error, lost frames) computed by the existing Step-8 `BenchmarkMetrics`.
- **non-interference** (mandatory) — for all 5 scenarios a bare `SimulationRunner` and the
  `DemoSession` produce field-identical `SimulationStepResult` sequences. `fsoc_step11_tests`
  green (23 checks). Demo numbers match Step 10 exactly.
- reproducible: `make demo` (or `scripts/run_baseline_demo.sh`) runs the Step-10 validation
  + the static & sinusoidal demos + the Step-9 visualization evidence. See
  `docs/17_DEMO_FREEZE.md` for the teammate-Mac checklist.

## AI Perception (V2) implemented — real trained model, real C++ inference, safety-gated

Additive, post-`v1_baseline` work on `feat/ai-perception`. The frozen classical
baseline above is **unchanged** by any of this — see `docs/19_AI_PERCEPTION_ARCHITECTURE.md`
and ADR-015/016/017/018 in `DECISIONS.md` for the full design history.

- **A real trained model, not a stub.** `TinyBeaconNet` (27,282 parameters, a small
  fully-convolutional heatmap network — not a downloaded/pretrained backbone) is trained
  on a deterministic, seeded synthetic dataset (`fsoc_ai_datagen`, domain-randomized
  noise/blur/clutter/distractors), exported to ONNX (opset 12), and loaded natively in
  C++ via OpenCV-DNN (`AiBeaconDetector`, `include/fsoc/ai_beacon_detector.hpp`). No
  Python is in the runtime path. Python↔ONNX Runtime↔C++ numeric parity is measured, not
  assumed: centroid agreement is **1.54e-7 px** on this machine (`fsoc_ai_beacon_detector_tests`).
- **Safe Hybrid fusion (ADR-018), not a naive confidence blend.** `resolve_perception()`
  implements a frozen decision table: classical+AI agreement (≤8.0 px) accepts the
  classical centroid; classical-only accepts classical; **AI-only and any classical/AI
  disagreement both reject unconditionally** — no confidence override, no averaging,
  no "trust whichever is brighter." Stage-2 evidence (`DECISIONS.md` ADR-018) showed AI
  confidence does not separate correct from wrong detections, so a lone high-confidence
  AI candidate is deliberately never trusted alone.
- **Measured, unflattering-where-true evaluation**, not marketing numbers.
  `stage4_evaluation` (`docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md`) scores Classical vs.
  AI vs. Hybrid across 11 deterministic degraded scenarios. Headline finding: Hybrid
  alone reduces severe closed-loop wrong-lock outliers by ~20% vs. Classical, but does
  **not** fix Classical's own bright-clutter false-lock vulnerability (44.9% aggregate
  common-frame false-positive rate) unaided — full numbers and the caveats discovered
  while producing them are in `docs/MVP_METRICS.md`.
- **Reachable from the actual demo, not just from unit tests.**
  `fsoc_demo <scenario> --mode classical|ai|hybrid` runs the same validated closed loop
  with AI/Hybrid perception live; falls back to classical with a visible warning if the
  ONNX model can't be loaded (Phase-7-style failure handling, not a crash). Mission
  Control's telemetry rail shows the live mode/source/AI-confidence/rejection-reason.

## State estimation + clutter mitigation (P0-v2) — measured, not just implemented

Additive, post-Stage-4 work, still on `feat/ai-perception`. Closes the two gaps the AI
Perception section above states plainly (no motion filter; Hybrid alone doesn't fix
Classical's clutter false-lock): `fsoc::TargetTracker`, a minimal alpha-beta (g-h) state
estimator with a temporal-consistency gate — **not** a Kalman/UKF, a deliberate choice
(`include/fsoc/target_tracker.hpp`). Additive and default-off (`tracker_enabled = false`
/ `fsoc_demo --tracker`); every seam is proven bit-identical when disabled by a
dedicated regression test.

- **Root cause found and fixed, not guessed.** Classical's clutter vulnerability traced
  to a bad-acquisition mechanism: 3 consecutive detections confirmed a track even when
  they disagreed spatially. Fixed in the estimator's acquisition logic
  (`DECISIONS.md` ADR-019).
- **Measured mitigation** (`docs/MVP_ABLATION.md`, `stage4_tracker_ablation`, full frozen
  Stage-4 protocol, 22,000 frames/config): severe (>50px) closed-loop outliers fall from
  2,240 (Classical) / 1,808 (Hybrid) to 12 / 9 — a **99.5% reduction** — at a real,
  disclosed coverage cost (~20 points). The intrinsic 44.9% single-frame FPR is
  unchanged (unfixable without touching the frozen classical detector algorithm).
- **A real, disclosed limit, not hidden**: a *temporally coherent* (smoothly moving)
  distractor defeats this mitigation completely (`docs/MVP_ABLATION.md §6`,
  `mvp_dynamic_scenarios`) — the gate rejects spatially/temporally incoherent
  candidates, not any adversarial one.
- **5 named, deterministic demo presets** — `fsoc_demo normal|noise|occlusion|clutter|reacquisition`
  (`docs/MVP_GOLDEN_DEMO.md`) — each a self-contained, reproducible condition tied to a
  specific measured finding above.
- **Full latency budget measured** (`docs/MVP_METRICS.md §5`, `mvp_latency_budget`):
  every configuration, including Hybrid+Tracker, fits inside the 20 ms / 50 Hz budget
  at P95 on this development machine (not a hardware claim).
- The 27-column Step-8 telemetry CSV now carries 42 columns total (7 Stage-3 perception
  + 8 P0-v2 tracker fields, both additive and header-driven — old readers unaffected);
  Mission Control's telemetry rail gained a live "STATE ESTIMATOR" panel.

## macOS quick start

```bash
xcode-select --install      # only if Command Line Tools are missing
brew install cmake ninja
brew install opencv          # required from Step 4 onward

cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/step1_math_smoke
./build/debug/step2_trajectory_smoke
./build/debug/step3_observation_smoke
./build/debug/step4_renderer_smoke
./build/debug/step5_detector_smoke
./build/debug/step6_pid_smoke
./build/debug/step7_closed_loop_smoke
./build/debug/step8_telemetry_smoke      # writes generated/step8_*.csv
./build/debug/step9_visualization_smoke  # writes generated/step9/*.png (+ optional .mp4)
./build/debug/step10_validation_smoke    # baseline acceptance; writes generated/step10/
./build/debug/fsoc_demo sinusoidal       # demo runner: static|sinusoidal|loss|open|closed
./build/debug/fsoc_demo static --mode hybrid   # same demo, live AI + Safe Hybrid perception
./build/debug/fsoc_demo static --mode hybrid --tracker  # + P0-v2 state estimator (Hybrid V2)
./build/debug/fsoc_demo clutter          # named disturbance preset: normal|noise|occlusion|clutter|reacquisition
make demo                                # reproducible: validation + demos + visualization

# AI perception (requires the committed models/tiny_beacon_net.onnx, already in the repo)
./build/debug/ai_inference_benchmark             # C++ ONNX inference latency, this machine
cmake --preset release && cmake --build --preset release
./build/release/stage4_evaluation --out generated/ai_stage4   # full frozen-protocol eval, ~10-15 min
./build/release/stage4_tracker_ablation --out generated/ai_stage4_ablation  # P0-v2 clutter mitigation, ~15 min
./build/release/mvp_dynamic_scenarios --out generated/mvp_dynamic_scenarios # velocity/dropout/moving-clutter scenarios
./build/release/mvp_latency_budget                                          # full latency budget, seconds

# Mission Control frontend (Next.js; reads real fsoc_demo CSV output, never fakes telemetry)
cd frontend && npm install
npm run dev          # http://localhost:4317 — toggle ENGINE (live fsoc_demo) / REPLAY (checked-in fixture)
npm run typecheck && npm run lint && npm run build
npx playwright test  # end-to-end smoke suite, incl. a no-Math.random anti-fake-data guard
```

Steps 1–3 build and pass without OpenCV; if `opencv` is missing, CMake prints a notice
and skips the Step 4 renderer target only. Install it with `brew install opencv` and
reconfigure — no Homebrew paths are hardcoded.

## Simulation vs. real hardware, and current limitations

Everything in this repository — every metric, every demo, every frontend view — runs
against the deterministic C++ **simulation** (`SyntheticCameraRenderer` draws an analytic
Gaussian beacon; there is no physical camera, beacon, or pan/tilt mechanism anywhere in
this codebase). Mission Control's `ENGINE`/`REPLAY` toggle distinguishes "the real
simulation binary, run live" from "a checked-in deterministic recording of that same
binary" — neither is a physical test bench. See `docs/MVP_METRICS.md` §5 for the full
"not measured / not claimed" list.

The architecture is deliberately layered so `FrameSource` / `Detector` / `Controller` /
`PanTiltCamera` are independently swappable (`docs/09_FUTURE_ARCHITECTURE.md`): the next
hardware step is replacing `SyntheticCameraRenderer` with a real frame grabber and
`PanTiltCamera::step()`'s actuator model with a real servo/motor driver, without touching
the detector, PID, or `SimulationRunner` step order. Known MVP-stage limitations: AI
recall is intentionally low (~16-40% depending on scenario) rather than over-confident;
a minimal alpha-beta state estimator now exists and measurably mitigates (does not
solve) Classical's clutter false-lock behavior for spatially/temporally *incoherent*
candidates — a *temporally coherent* (smoothly moving) distractor still defeats it
completely, a real, disclosed, currently-unresolved gap (`docs/MVP_ABLATION.md`); the
estimator is a P-dominant plant + alpha-beta filter (kp=12, ki=0, kd=0), not a Kalman/UKF
— a deliberate, documented, currently-sufficient choice
(`docs/16_BASELINE_ACCEPTANCE.md`, `include/fsoc/target_tracker.hpp`), not an oversight.

## Repository layout

```text
include/fsoc/       Public interfaces
src/                Core implementations
apps/               Executable simulation/demo/benchmark/evaluation programs
tests/              Mathematical/unit validation (CTest)
models/             Committed trained ONNX model + metadata (models/MODEL_CARD.md)
tools/ai/           Offline Python training toolchain (NOT part of the C++ runtime)
frontend/           Next.js Mission Control UI (reads real fsoc_demo telemetry)
cmake/              Build policies
.claude/skills/     Claude Code engineering skills
.claude/agents/     Specialist subagents
docs/               PRD/SRS/design/roadmap/test plans/AI architecture/measured metrics
prompts/             Reusable Vibe Coding prompts
generated/           Git-ignored run artifacts (CSV/PNG/JSON reports) — never committed
```

Read `CLAUDE.md` before asking an AI coding agent to modify the project.
