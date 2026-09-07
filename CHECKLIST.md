# FSOC Engineering Checklist — C++20

(Originally the "48-Hour MVP Checklist" for the initial baseline sprint; retitled as
the project grew through Stage 2-4 AI perception and P0-v2 state estimation. Content
below is a chronological, `[x]`-per-milestone engineering log, not a to-do list.)

- [x] CMake/C++20 skeleton
- [x] Frozen coordinate convention
- [x] Vec3 geometry primitives
- [x] Virtual pan/tilt camera
- [x] Narrow FOV + pinhole projection
- [x] Velocity limits + tilt stops
- [x] Step-1 unit checks
- [x] Terminal math smoke test
- [x] Trajectory interface + stationary + linear trajectory
- [x] Sinusoidal trajectory
- [x] Measurement/error data contracts (observation / detection / tracking-error, frozen sign conventions)
- [x] Synthetic grayscale beacon renderer (OpenCV C++) — CV_8UC1, sub-pixel Gaussian beacon
- [x] Threshold + centroid detector — connected-components + intensity-weighted centroid, ~0.02 px on clean frames
- [x] Independent pan/tilt PID controller — `fsoc_control` (OpenCV-free), angular error -> rate command, anti-windup, reset
- [x] Closed-loop SimulationRunner — `fsoc_simulation`, deterministic fixed-step, pixel-only feedback, static acq final error ~0 deg
- [x] Telemetry logger — `fsoc_telemetry`, observer-only `TelemetryRecord` + synchronous `CsvTelemetryLogger` (27 cols)
- [x] FPS and timing measurement — `std::chrono` wall clock, separate from the fixed sim dt (~4700 FPS, ~90x real time)
- [x] Target-loss behavior — no detection -> PID reset + zero command + camera holds (no search mode)
- [x] Rate saturation telemetry — `pan_saturated` / `tilt_saturated` + `command_saturation_fraction` benchmark metric
- [x] Parameterized benchmark scenarios — static / linear / sinusoidal closed / sinusoidal open, `BenchmarkMetrics` (RMS / P95 / max)
- [x] OpenCV visualization — `fsoc_visualization`, observer-only `TrackingVisualizer` (CV_8UC1 -> annotated CV_8UC3), headless PNG/MP4 export
- [x] Plot/export telemetry for judging — `fsoc_validation` suite writes per-scenario 27-col CSV + annotated PNG + `generated/step10/VALIDATION_REPORT.md` + judge-friendly summary table
- [x] Baseline acceptance metrics met — 7/7 scenarios PASS (Static / Slow-Linear / Sinusoidal / Near-FOV-Edge / Actuator-Saturation / Loss-and-Re-entry / Open-vs-Closed); gates frozen in `docs/16_BASELINE_ACCEPTANCE.md`; `step10_validation_smoke` prints `STEP 10 BASELINE ACCEPTANCE: PASS`
- [x] Freeze `v1_baseline` — **tag created and pushed to `origin`** (points at the merged Step‑10 baseline `20c028c`); the validated Step‑10 baseline is FROZEN. Step‑11 demo/frontend packaging is additive work layered on top and does not move the tag.
- [x] Demo scenario presets — `DemoScenario` (static / sinusoidal / loss / open / closed) + `parse_demo_scenario`, reusing the validated Step-10 parameters verbatim
- [x] Frontend snapshot contract — `DemoSnapshot` view model built only from `SimulationStepResult` + `TelemetryRecord` + `CameraConfig` (`fsoc_demo_support`); radians internal, `to_degrees()` at the UI boundary; documented in `docs/18_FRONTEND_DATA_CONTRACT.md`
- [x] Demo session runner — `DemoSession` (deterministic, owns trajectory + `SimulationRunner`; `DemoRunState` Ready/Running/Paused/Finished; paused `step()` does not advance sim time); non-interference proven vs a bare `SimulationRunner`
- [x] Demo CLI — `apps/fsoc_demo.cpp` (`static|sinusoidal|loss|open|closed`, `--help`, `--csv`, `--duration`, `--quiet`); reuses the Step-8 telemetry + benchmark code
- [x] Reproducible demo workflow — `make demo` / `scripts/run_baseline_demo.sh` (validation + static + sinusoidal demos + Step-9 visualization evidence)
- [x] Demo freeze doc + teammate Mac checklist — `docs/17_DEMO_FREEZE.md`
- [x] Step-11 tests — `fsoc_step11_tests` (23 checks: snapshot copy, radians, `to_degrees`, session convergence, open==closed trajectory, reset replay, pause-no-advance, non-interference, baseline PID unchanged, Step-10 still PASS)

## V2 — AI PERCEPTION (`feat/ai-perception`, post-`v1_baseline`)

- [x] AI design + integration decisions — `DECISIONS.md` ADR-015 (learned detector behind the frozen `BeaconDetection` contract; TinyBeaconNet, not YOLO; ONNX→OpenCV-DNN; synthetic-only), ADR-016 (additive `PerceptionMode` strategy seam, default = Classical, bit-identical regression), and ADR-018 (post-Stage-2 safety revision: AI is candidate perception only — Safe Hybrid policy frozen, `agreement_radius_px`=8.0px, AI-only/disagreement both reject unconditionally; documentation only, Stage 3 not started)
- [x] Synthetic dataset generator — `fsoc_ai_datagen` (`fsoc/ai_frame_synth.hpp` + `src/ai_frame_synth.cpp`): pure seeded domain randomization (beacon peak/σ/anisotropy/edge-clip, background gradient + vignette, Gaussian read + shot-like noise, hot/dead/salt pixels, Gaussian blur / defocus / motion blur, star clutter + bright distractor + cluster, negatives); frozen 8-stage order; `synthesize(seed)` byte-reproducible
- [x] Dataset CLI — `apps/generate_ai_dataset.cpp` → `generate_ai_dataset` (contiguous disjoint train/val/test blocks over one global index space, per-sample `sample_seed_for`, evenly-spaced negatives, JSONL label manifests + `dataset.json` reproducibility manifest); output git-ignored under `generated/ai_dataset/`
- [x] AI datagen tests — `fsoc_ai_datagen_tests` (config validation, seed determinism + 20k collision-free, byte-identical frames per seed, force-target override, positive-has-signal / negative-is-empty, negative-fraction respected, edge-clip both ways, difficulty bounds)
- [x] Python training toolchain scaffold — `tools/ai/` (`common.py` frozen preprocessing/heatmap/decode, `model.py` TinyBeaconNet **27,282 params**, `dataset.py`, `train_beacon_net.py`, `eval_beacon_net.py` val-threshold calibration, `export_onnx.py`, `make_parity_fixture.py`, `selfcheck.py`, `requirements.txt`, `README.md`)
- [x] AI docs — `docs/19_AI_PERCEPTION_ARCHITECTURE.md`, `docs/20_AI_DATASET_AND_TRAINING.md`
- [x] **Stage 2** — Train TinyBeaconNet + calibrate threshold + export ONNX + model card. ADR-017 (foreground-weighted heatmap loss + global-max presence pool — plain MSE stalls). Dataset regenerated + integrity-checked (8400/8400 unique hashes, labels visually verified ≈0.04 px). Best epoch 9; **frozen presence threshold 0.95** (val only). Test @ 0.95: presence precision 0.989 / recall 0.404 / FPR 0.013; committed-detection centroid **median 1.71 px, 97.5 % ≤ 10 px**. `models/tiny_beacon_net.onnx` (110 KiB, opset 12, `onnx.checker` OK), torch↔ORT parity max\|Δ\| ≤ 8e-6 / 2e-6 px. `models/MODEL_CARD.md`. Known limit: recall ≈ 40 % (presence-head ceiling, 10 variants tried) — recall deferred to Stage-3 hybrid + Phase-2 temporal. Step-10 still PASS.
- [x] **Stage 3** — C++ AI detector `fsoc_ai_perception` (`AiBeaconDetector`, OpenCV-DNN ONNX) with model-file/output-contract validation; Python↔C++ preprocessing parity (0 count diff) and centroid parity (Euclidean diff 1.54e-7 px, ≪0.1 px target); repeated-inference determinism verified; C++ inference latency measured (mean 1.010 ms / P95 1.269 ms, this machine)
- [x] **Stage 3** — Hybrid detector + `PerceptionMode` / `PerceptionSource` / `PerceptionRejectionReason`; Safe Hybrid policy (ADR-018) implemented exactly (`resolve_perception()`, `agreement_radius_px` = 8.0 px frozen, no confidence override, AI-only and disagreement both reject unconditionally) — 25+ dedicated tests across `fsoc_hybrid_perception_tests` / `fsoc_perception_integration_tests`
- [x] **Stage 3** — `SimulationRunner` perception seam (default Classical, bit-identical regression proven both by test and by Step-10 numerical identity) + AI telemetry fields (`TelemetryRecord` +7 columns, 27→34, header-driven so old readers are unaffected)
- [x] **Stage 4** — AI evaluation suite `fsoc_stage4_eval` / `stage4_evaluation` — scenario families A–K, CLASSICAL vs AI vs HYBRID common-frame + closed-loop tables, C++ inference latency per mode; frozen protocol `docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md`; real, unflattering-where-true findings (see `docs/MVP_METRICS.md`)
- [x] Frontend AI integration (MVP scope) — `fsoc_demo --mode classical|ai|hybrid` (graceful fallback to classical if the model can't load), live perception telemetry through the CSV bridge, Mission Control "PERCEPTION (STAGE 3)" panel (mode/source/AI confidence/rejection reason). **Not done**: dedicated architecture/benchmark pages for the AI pipeline, heatmap overlay — tracked as P2 polish, not MVP-blocking.
- [x] `docs/MVP_METRICS.md` (consolidated real measured numbers, Stage 1-4 + frontend) + `docs/08_TELEMETRY_SCHEMA.md` / `docs/15_INTERFACE_CONTRACTS.md` / `docs/16_BASELINE_ACCEPTANCE.md` / `docs/17_DEMO_FREEZE.md` updated for the 34-column schema and `--mode` flag (superseded by 42 columns, P0-v2 below)

## P0-v2 — STATE ESTIMATION + CLUTTER MITIGATION (`feat/ai-perception`, post-Stage-4)

- [x] `fsoc::TargetTracker` — alpha-beta (g-h) state estimator + temporal-consistency gate (`include/fsoc/target_tracker.hpp`/`.cpp`), deliberately not Kalman/UKF; `LockState` {Searching, Acquiring, Tracking, Coasting, Lost}, kept separate from the frozen `fsoc::TrackingState`; parameters empirically grounded against measured Step-10 frame-to-frame deltas, not guessed; 19 dedicated unit tests (`fsoc_target_tracker_tests`)
- [x] `is_safe_to_steer()` — explicit, separate control-safety predicate (Tracking always safe; Coasting safe only above a confidence threshold)
- [x] `SimulationRunnerConfig::tracker_enabled` seam — additive, default off, bit-identical-when-disabled regression tests in both `tests/step7_tests.cpp` and `tests/step11_tests.cpp`
- [x] Root-cause clutter investigation + fix — acquisition now requires spatial consistency, not just presence (`DECISIONS.md` ADR-019); found and fixed via the new `stage4_tracker_ablation` tool, not assumed
- [x] `stage4_tracker_ablation` + `docs/MVP_ABLATION.md` — full frozen Stage-4 protocol (22,000 frames/config): severe outliers 2,240→12 (Classical) / 1,808→9 (Hybrid), **99.5% reduction**, real disclosed coverage cost (~20 points); "Hybrid V2" = Hybrid + tracker, not a new `PerceptionMode`
- [x] `mvp_dynamic_scenarios` (Phase G) — velocity, direction change, 1/2/10-frame dropout, moving distractor, overexposure proxy, edge-of-frame; **discloses, does not hide**, that a temporally coherent moving distractor defeats the mitigation completely
- [x] `mvp_latency_budget` (Phase L) — full stage-by-stage + end-to-end latency budget; every configuration fits the 20ms/50Hz budget at P95 on this development machine (explicitly not a hardware claim)
- [x] `fsoc_demo --tracker` CLI flag + 5 named disturbance presets (`normal|noise|occlusion|clutter|reacquisition`, `docs/MVP_GOLDEN_DEMO.md`) — deterministic, one-command, each tied to a specific measured finding above
- [x] Telemetry: 8 additive tracker columns (34→42 total, header-driven, old readers unaffected); Mission Control "STATE ESTIMATOR (P0-v2)" panel wired to LOCAL ENGINE mode; a real, previously-latent bug (AI/Hybrid mode never actually reachable from the browser — wrong subprocess CWD) found and fixed while wiring this
- [x] Frontend regression: 20/20 Playwright (18 prior + 2 new, incl. the engine-mode tracker/mode regression guard)
- [x] `docs/MVP_GOLDEN_DEMO.md` — 15-step reproducible judge walkthrough, verified end-to-end
- [x] `DECISIONS.md` ADR-019 — records the ADR-018 §9 temporal gate as now implemented; AI-only reacquisition through `resolve_perception()` itself remains explicitly out of scope
