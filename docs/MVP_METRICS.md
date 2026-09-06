# MVP Metrics (measured, not assumed)

Every number below was produced by running a committed, deterministic test/benchmark
binary on this development machine and reading its actual output — none is estimated,
extrapolated, or asserted from memory. All results are **simulated-environment**
results (the deterministic C++ engine in `src/`, `docs/04_COORDINATES_AND_MATH.md`).
**No physical camera, beacon, or pan/tilt hardware has been used or is implied.**
See `docs/16_BASELINE_ACCEPTANCE.md` for the full classical-baseline acceptance
report this file summarizes and extends with the Stage-3/4 AI results.

## Environment

| | |
|---|---|
| Commit | `9ae89ac` (branch `feat/ai-perception`) |
| Machine | Apple M5, macOS 26.6.2, arm64 |
| Compiler | AppleClang 21.0.0 |
| CMake | 4.4.3, Ninja generator |
| Build type | Debug (`cmake --preset debug`) unless noted **Release** |
| OpenCV | 5.0.0 (Homebrew) |
| Simulation clock | fixed 50 Hz (`dt = 0.02 s`) — never wall-clock |

## 1. Classical baseline — Step-10 acceptance suite

Method: `./build/debug/step10_validation_smoke`, gates frozen in
`docs/16_BASELINE_ACCEPTANCE.md` before the run.

| scenario | detection | RMS | P95 | max | lost | result |
|---|---|---|---|---|---|---|
| Static Acquisition | 100.0% | 0.4691° | 0.2885° | 4.1275° | 0 | PASS |
| Slow Linear Tracking | 100.0% | 0.3908° | 0.1028° | 4.8863° | 0 | PASS |
| Sinusoidal Tracking | 100.0% | 0.5461° | 0.7899° | 0.7982° | 0 | PASS |
| Near-FOV-Edge Acquisition | 100.0% | 1.5613° | 2.8305° | 9.9273° | 0 | PASS |
| Actuator Saturation | 100.0% | 1.8189° | 3.2724° | 11.3779° | 0 | PASS |
| Target Loss and Re-entry | 73.2% | 4.9328° | 8.8284° | 9.9692° | 107 | PASS |
| Open vs Closed Loop | 100.0% | 0.5461° | 0.7899° | 0.7982° | 0 | PASS |

Open-loop vs. closed-loop, same trajectory: detection 57.4% → 100.0%, RMS error
6.4549° → 0.5461° (11.8× better), lost frames 426 → 0. Result: **7/7 PASS**,
`STEP 10 BASELINE ACCEPTANCE: PASS`. Determinism verified (two independent runs of
each scenario are bit-identical, per `docs/15_INTERFACE_CONTRACTS.md`).

## 2. Classical-vs-AI-vs-Hybrid perception — Stage-4 evaluation

Method: `./build/release/stage4_evaluation` (Release build — a Debug build's
unoptimized per-pixel synthetic-noise loops make the full frozen-protocol run
impractically slow; the underlying algorithms are identical in both configurations
and Debug-build correctness is separately verified by `ctest`). Full frozen protocol:
`docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md`. 11 degraded scenarios × 5 seeds.

**Common-frame benchmark** (11,000 frames, 7,995 truth-positive / 3,005 truth-negative):

| metric | Classical | AI | Safe Hybrid |
|---|---|---|---|
| accepted rate | 77.05% | 11.75% | 76.83% |
| recall | 89.12% | 16.01% | 88.82% |
| precision | 84.07% | 99.07% | 84.03% |
| false-positive rate | 44.93% | 0.40% | 44.93% |
| median localization error | 0.020 px | 1.630 px | 0.020 px |
| P95 localization error | 1.037 px | 3.681 px | 0.904 px |
| severe outliers (>50px / >100px) | 304 / 286 | 19 / 19 | 290 / 273 |

**Closed-loop benchmark** (22,000 steps/mode, 8s × 11 scenarios × 5 seeds):

| metric | Classical | AI | Safe Hybrid |
|---|---|---|---|
| accepted-detection fraction | 99.69% | 15.81% | 97.35% |
| RMS angular error | 3.257° | 3.716° | 3.116° |
| severe control-pointing outliers (>50px) | 2,240 | 869 | 1,808 (**−19.3%** vs Classical) |
| perception latency, mean / P95 | 0.251 / 0.380 ms | 0.956 / 1.204 ms | 1.215 / 1.542 ms |

**Headline, unflattering-where-true finding:** Classical's naive "brightest connected
component" rule false-locks onto bright clutter far more than expected in isolation
(44.9% aggregate FPR, up to 708 px error) — Safe Hybrid only partially mitigates this
(≈20% fewer severe closed-loop outliers) because it inherits Classical's output
whenever AI abstains, which Stage-4 shows is the majority of frames. Full analysis,
including two known scenario-config caveats discovered during evaluation and left
uncorrected per the frozen-protocol rule: `docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md`
and the Stage-4 checkpoint report in this session's history.

## 3. TinyBeaconNet — training, export, and C++ inference

| metric | value | source |
|---|---|---|
| parameters | 27,282 | `models/tiny_beacon_net.meta.json` |
| training epochs to best checkpoint | 9 | `models/training_summary.txt` |
| presence threshold (val-calibrated, frozen) | 0.95 | `models/threshold.json` |
| test precision / recall / FPR @ threshold | 98.91% / 40.44% / 1.33% | `models/stage2_test_report.json` |
| PyTorch ↔ ONNX Runtime parity (train-time) | ≤ 8e-6 (presence) / 2e-6 px (centroid) | `models/MODEL_CARD.md` |
| **ONNX Runtime ↔ C++ OpenCV-DNN parity** (this machine) | presence logit Δ 9.5e-7, centroid Δ **1.54e-7 px** | `tests/ai_beacon_detector_tests.cpp` (CTest) |
| **C++ inference latency** (this machine, CPU, 500 samples post-warmup) | mean 1.010 ms, median 0.986 ms, P95 1.269 ms, max 1.838 ms | `ai_inference_benchmark` |

P95 inference latency (1.27 ms) fits well inside the 50 Hz / 20 ms simulation frame
budget on this machine. This is **not** a hardware real-time-loop qualification claim
— no physical camera or actuator has been driven by this inference path.

## 4. Frontend / demo pipeline

| metric | value | method |
|---|---|---|
| C++ → CSV → frontend telemetry columns | 34 (27 core + 7 Stage-3 perception, additive) | `docs/08_TELEMETRY_SCHEMA.md` |
| Frontend typecheck / lint / build | clean | `npm run typecheck / lint / build` |
| Playwright E2E | 18/18 passed | `npx playwright test`, incl. the repo's own "no Math.random in application source" guard |
| Simulation processing throughput (informational — NOT the 50 Hz sim rate) | ~3,400–3,900 FPS | `fsoc_demo` wall-clock timer around the step loop |

## 5. What is NOT measured / NOT claimed

- No physical camera, beacon, servo, or pan/tilt hardware has been used. Every number
  above is from the deterministic C++ simulation.
- No claim of "real-time on embedded hardware" — latency was measured on a desktop-class
  Apple M5, not a target embedded platform.
- AI recall (~16–40% depending on evaluation) is a known, documented, reported limitation
  — not hidden, not a bug being tracked to zero before this MVP milestone.
- `Target Loss and Re-entry`'s 73.2% detection figure is **expected and correct** for that
  scenario's designed aggressive sweep-past-FOV trajectory — it is a stress scenario, not
  a regression.

## Reproduce

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
./build/debug/step10_validation_smoke
cmake --preset release && cmake --build --preset release
./build/release/ai_inference_benchmark
./build/release/stage4_evaluation --out generated/ai_stage4   # ~10-15 min, frozen protocol
cd frontend && npm run typecheck && npm run lint && npm run build && npx playwright test
```
