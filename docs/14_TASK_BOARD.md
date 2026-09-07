# Task Board

## Foundation
- [x] C++20/CMake scaffold
- [x] coordinate math
- [x] camera projection
- [x] actuator rate limits
- [x] native Step-1 tests

## Target motion truth
- [x] `TargetState` = position_m + velocity_mps (own header)
- [x] `Trajectory` abstract interface (time -> state, no owned clock)
- [x] stationary trajectory
- [x] linear constant-velocity trajectory
- [x] sinusoidal trajectory with analytic velocity
- [x] deterministic analytic tests + smoke

## Observation / measurement contracts
- [x] `ImagePoint` + frozen image convention (origin top-left, +x right, +y down)
- [x] `CameraObservation` / `ObservationStatus` (Visible / OutsideFieldOfView / BehindCamera)
- [x] `observe_beacon()` reuses `PanTiltCamera::project()` (no duplicated projection math)
- [x] `BeaconDetection` (centroid only; no fabricated confidence)
- [x] `PixelError` / `AngularError` / `TrackingError` with frozen sign conventions
- [x] `compute_tracking_error()` — `std::optional` in / out, non-finite rejected
- [x] `ScenarioConfig` data contract (mode, duration_s, fixed timestep_s)

## Perception baseline
- [x] OpenCV renderer — `fsoc_render` lib (isolated), `SyntheticCameraRenderer`
- [x] CV_8UC1 grayscale, background + sub-pixel Gaussian beacon (sigma in px)
- [x] edge-safe clipped raster window; non-Visible -> background-only
- [x] test-only weighted-centroid recovers requested sub-pixel location
- [x] threshold detector — `fsoc_perception` lib, `BeaconDetector` (pixels only, no renderer dep)
- [x] centroid — 8-connected components + intensity-weighted centroid, `weight=(pixel-threshold)+1`
- [x] strongest-integrated-signal component selection (deterministic tie-break)
- [x] loss state — `std::nullopt` on no bright pixels / no component >= min size
- [x] input validation — empty / non-CV_8UC1 rejected with `std::invalid_argument`

## Control
- [x] PID class — `fsoc_control` lib (OpenCV-free, `fsoc::core` only), `PIDController`
- [x] `ControlCommand` (pan/tilt rate in rad/s) + `zero_control_command()`
- [x] independent pan/tilt axes; `u = kp*e + ki*I + kd*D` on angular error
- [x] derivative forced to 0 on first update after construction/reset
- [x] anti-windup — integral hard clamp + conditional integration; output clamp
- [x] `reset()` clears integral / previous error / first-sample flags
- [x] validation — bad `dt_s` / config / non-finite `TrackingError` -> `std::invalid_argument`

## Closed-loop integration
- [x] `fsoc_simulation` lib — `SimulationRunner` wires trajectory -> observe -> render ->
      detect -> tracking error -> PID -> camera.step (the one integration layer)
- [x] deterministic fixed-step (dt = 0.02 s, 50 Hz); `step()` + `run_for()` + `reset()`
- [x] `SimulationStepResult` (truth fields labelled; control path never reads them)
- [x] pixel-only feedback verified — control follows detected centroid, not projection truth
- [x] target-loss policy — PID reset + zero command + camera holds; reappearance resumes
- [x] `SimulationMetrics` + `evaluate()` (detection %, RMS/max/final angular error, lost frames)
- [x] open- vs closed-loop comparison (`control_enabled`)
- [x] empirically-tuned MVP baseline gains (kp=12, ki=0, kd=0)

## Evidence
- [x] telemetry CSV — `fsoc_telemetry` lib; `TelemetryRecord` (27 cols) + `CsvTelemetryLogger`
- [x] observer-only, non-interference proven (with/without telemetry -> identical results)
- [x] performance metrics — `BenchmarkMetrics` (RMS/mean/max/final/P95 angular, pixel error,
      saturation fractions, rates); wall clock via `std::chrono`, separate from sim dt
- [x] scenario suite — static / linear / sinusoidal closed / sinusoidal open benchmarks
- [x] `step8_telemetry_smoke` writes generated/step8_*.csv + prints the comparison table
- [x] demo overlay — `fsoc_visualization` lib; `TrackingVisualizer` (CV_8UC1 -> annotated CV_8UC3)
- [x] observer-only, perception frame never touched; mandatory non-interference test passed
- [x] overlays: crosshair / detection marker / error vector / status / HUD (attitude, errors, rates, SAT)
- [x] headless PNG sequence (required) + optional best-effort MP4; output -> generated/step9/
- [x] `step9_visualization_smoke` — static acquisition + sinusoidal + target-lost frames

## Baseline acceptance
- [x] `fsoc_validation` lib — evaluation layer over `fsoc::simulation` + `telemetry` + `visualization`; no domain math, never controls the loop
- [x] acceptance gates frozen up front in `docs/16_BASELINE_ACCEPTANCE.md` (not derived from the scored run); baseline PID unchanged (kp=12, ki=0, kd=0)
- [x] 7 named scenarios — static / slow-linear / sinusoidal / near-FOV-edge / actuator-saturation / target-loss+re-entry / open-vs-closed
- [x] per-scenario global checks — finite (no NaN/Inf), monotonic timestamps, fixed dt, command <= PID limit, applied <= actuator limit, target-loss semantics
- [x] determinism — every scenario run twice, metrics + `SimulationStepResult` sequence bit-identical
- [x] MANDATORY failure-check test — impossible gate + tightened real threshold -> `evaluate_passed()` and `overall_passed` both false
- [x] `step10_validation_smoke` — judge table + CSV/PNG evidence + `generated/step10/VALIDATION_REPORT.md`; ends `STEP 10 BASELINE ACCEPTANCE: PASS` (7/7)
- [x] baseline acceptance metrics met — static 4.13 deg -> 0.00 deg, sinusoidal RMS 0.55 deg, open->closed detection 57.4% -> 100%, RMS x11.8
- [x] freeze `v1_baseline` — **tag created and pushed to `origin`**, pointing at the merged Step‑10 baseline (`20c028c`); the validated baseline is FROZEN and the tag is not moved by later work

## Demo freeze / frontend prep (Step 11 — additive)
- [x] `fsoc_demo_support` lib — links `fsoc::simulation` + `fsoc::telemetry`; pure observer, no physics/control/JSON/networking
- [x] `DemoScenario` presets — static / sinusoidal / loss / open / closed + `parse_demo_scenario`; reuse validated Step-10 params verbatim
- [x] open vs closed share identical `SinusoidalTrajectory` parameters — only `control_enabled` differs (tested)
- [x] `DemoSnapshot` view model — built only from `SimulationStepResult` + `TelemetryRecord` + `CameraConfig`; optionals `std::nullopt` when lost
- [x] units — radians internal; `to_degrees(DemoSnapshot)` at the UI boundary only; core physics units unchanged
- [x] `DemoSession` — owns trajectory (before runner) + `SimulationRunner`; `DemoRunState` != `TrackingState`; paused `step()` does not advance sim time; `reset()` -> identical replay
- [x] `fsoc_demo` CLI — `static|sinusoidal|loss|open|closed`, `--help/--csv/--duration/--quiet`; summary via existing `BenchmarkMetrics`
- [x] MANDATORY non-interference — bare `SimulationRunner` vs `DemoSession` give field-identical `SimulationStepResult` sequences (all 5 scenarios)
- [x] `fsoc_step11_tests` — 23 checks; Step-10 `ValidationSuite` still overall PASS; baseline PID still 12/0/0
- [x] `make demo` / `scripts/run_baseline_demo.sh` — validation + demos + Step-9 visualization; `docs/17_DEMO_FREEZE.md` + `docs/18_FRONTEND_DATA_CONTRACT.md`

## V2 — AI PERCEPTION (`feat/ai-perception`, post-`v1_baseline`; ADR-015 / ADR-016)
- [x] `fsoc_ai_datagen` lib — `fsoc/ai_frame_synth.hpp` + `src/ai_frame_synth.cpp`; pure seeded domain-randomization synthesizer (8-stage frozen order), re-uses the analytic Gaussian beacon, no dependency on `fsoc_render`
- [x] `generate_ai_dataset` CLI — contiguous disjoint train/val/test blocks, `sample_seed_for(26169, i)`, evenly-spaced negatives, JSONL manifests + `dataset.json`; output git-ignored
- [x] `fsoc_ai_datagen_tests` — determinism / 20k collision-free seeds / positive-signal / empty-negative / negative-fraction / edge-clip / difficulty bounds
- [x] `tools/ai/` PyTorch toolchain — `common.py` (frozen preprocess/heatmap/decode), `model.py` (TinyBeaconNet ~50k params), `dataset.py`, `train_beacon_net.py`, `eval_beacon_net.py`, `export_onnx.py`, `make_parity_fixture.py`
- [x] `docs/19_AI_PERCEPTION_ARCHITECTURE.md`, `docs/20_AI_DATASET_AND_TRAINING.md`
- [ ] train + val-calibrate threshold + `models/tiny_beacon_net.onnx` + `MODEL_CARD.md`
- [ ] `fsoc_ai_perception` — `AiBeaconDetector` (OpenCV-DNN ONNX) + model-file validation + Python↔C++ parity tests
- [ ] `HybridDetector` + `PerceptionMode` / `PerceptionSource` + config-driven policy
- [ ] `SimulationRunner` perception seam (default Classical, bit-identical regression, Step-10 still PASS) + AI telemetry fields
- [ ] `fsoc_ai_validation` — scenario families A–K, CLASSICAL vs AI vs HYBRID table, C++ inference latency
- [ ] frontend AI integration (perception toggle, confidence/source/latency, heatmap overlay, benchmark + architecture views) + `docs/21_AI_VALIDATION.md`

## Post-baseline (later phases — NOT this one)
- [ ] spatio-temporal (3-frame) detector
- [ ] Eigen-backed UKF
- [ ] MPC
- [ ] vibration PSD model
- [ ] Zernike/phase disturbance
- [ ] satellite ephemeris + SGP4 coarse pointing
