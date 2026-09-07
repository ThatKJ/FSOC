# SIH MVP Freeze (V2)

**Status: FROZEN for SIH presentation.** No new models, architecture, algorithms, or
feature work is to be added on top of this state without deliberately unfreezing it.

Last functional/engineering commit: **`e6e3741`** ("docs(mvp): add Golden Demo V2
walkthrough (Phase K)" — the final code+doc commit of the MVP-V2 completion loop). This
freeze commit (`git log -1` on `main`/`feat/ai-perception` after it lands) adds **only**
this document, two stale-documentation fixes (`README.md`, `CHECKLIST.md`), and one
UI text clarity fix (Mission Control's top bar) — no algorithm, model, or architecture
changed. Every measured number below was re-verified against `e6e3741` in this freeze
pass, not carried over from memory.

## 1. Architecture (unchanged by this freeze)

```
Trajectory -> observe_beacon -> SyntheticCameraRenderer -> [demo disturbance, optional]
  -> {BeaconDetector | AiBeaconDetector} -> resolve_perception() (Safe Hybrid, ADR-018)
  -> TargetTracker (alpha-beta + temporal gate, optional, ADR-019) -> compute_tracking_error
  -> PIDController -> PanTiltCamera::step
```

Every stage above `PIDController` is swappable behind a frozen interface
(`docs/09_FUTURE_ARCHITECTURE.md`). `PerceptionMode`, `tracker_enabled`, and
`disturbance` are all additive seams, default-off, proven bit-identical to the
pre-existing behavior when disabled.

## 2. Build / Test Status (this freeze pass, re-verified)

| check | result |
|---|---|
| `cmake --build --preset debug` | clean |
| `cmake --build --preset release` | clean |
| `ctest --preset debug` | **100% passed, 17/17 suites** |
| `step10_validation_smoke` | **7/7 PASS** (Static 100.0%/0.4691°, Linear 100.0%/0.3908°, Sinusoidal 100.0%/0.5461°, Near-FOV-Edge 100.0%/1.5613°, Actuator-Saturation 100.0%/1.8189°, Loss-and-Reentry 73.2%/4.9328° — expected, a stress scenario, Open-vs-Closed 100.0%/0.5461°) |
| `stage4_evaluation` (frozen protocol, 11 scenarios x 5 seeds) | re-run in this pass — **bit-identical** to the committed numbers in `docs/MVP_METRICS.md §2` (Classical common-frame FPR 44.93%, closed-loop severe outliers 2,240/1,808 Classical/Hybrid) |
| `stage4_tracker_ablation` | re-run in this pass — **bit-identical** to `docs/MVP_ABLATION.md` (2,240→12, 1,808→9 severe outliers) |
| `mvp_dynamic_scenarios` | re-run in this pass — matches `docs/MVP_ABLATION.md §6` |
| `mvp_latency_budget` | re-run in this pass (uncontended): Hybrid+Tracker P95 **1.21 ms (6.1% of the 20 ms budget)** — see the methodology note in §4 |
| `frontend: typecheck / lint / build` | clean |
| `frontend: npx playwright test` | **20/20 passed** |
| 5 golden-demo presets (`normal/noise/occlusion/clutter/reacquisition`) | all exit 0, all match documented behavior exactly (detection %, RMS/P95/max angular error identical to `docs/MVP_GOLDEN_DEMO.md`) |
| CSV telemetry export | confirmed exactly **42 columns** |

## 3. Important Measured Metrics (all re-verified this pass)

| metric | value | source |
|---|---|---|
| Step-10 baseline acceptance | 7/7 PASS | `step10_validation_smoke` |
| Classical common-frame FPR | 44.93% (intrinsic, unchanged) | `stage4_evaluation` |
| AI parity (ONNX Runtime <-> C++) | centroid Δ 1.54e-7 px | `fsoc_ai_beacon_detector_tests` |
| Severe closed-loop outliers, Classical -> Classical+Tracker | 2,240 -> 12 (**-99.5%**) | `stage4_tracker_ablation` |
| Severe closed-loop outliers, Hybrid -> Hybrid+Tracker (V2) | 1,808 -> 9 (**-99.5%**) | `stage4_tracker_ablation` |
| Coverage cost of the above | -22.2 / -20.3 points respectively | `stage4_tracker_ablation` |
| Moving-distractor outliers, all 4 configs | identical 65/65/65/65 (**mitigation does not apply**) | `mvp_dynamic_scenarios` |
| Full-step latency, Hybrid+Tracker (V2), uncontended | mean 1.07 ms / P95 1.21 ms (6.1% of 20 ms budget) | `mvp_latency_budget` |
| Telemetry CSV columns | 42 (27 core + 7 perception + 8 tracker, additive) | `docs/08_TELEMETRY_SCHEMA.md` |
| C++ test suites | 17/17 (100%) | `ctest --preset debug` |
| Frontend E2E | 20/20 | `npx playwright test` |

## 4. Demo Commands (fastest judge procedure)

```bash
# one-time build
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset release && cmake --build --preset release

# the whole story in 5 commands (~10 seconds total)
./build/debug/fsoc_demo normal          # calm baseline
./build/debug/fsoc_demo clutter         # the problem, live, honestly framed
./build/debug/fsoc_demo occlusion       # short-gap bridging
./build/debug/fsoc_demo reacquisition   # full Lost -> reacquire state machine
cd frontend && npm run dev              # Mission Control, ENGINE mode, live diagnostic panel
```
Full 15-step narrative with what to say at each step: `docs/MVP_GOLDEN_DEMO.md`.

**Methodology note**: `mvp_latency_budget` is a wall-clock benchmark — do not run it
concurrently with another CPU-heavy process (this freeze pass measured 6.1% of budget
uncontended vs. 13.5% while `stage4_evaluation` ran in the background; both are true
statements about different conditions, only the uncontended number belongs on a slide).

## 5. Limitations (state these proactively, don't wait to be asked)

1. **A temporally coherent moving distractor defeats the clutter mitigation completely** — the single most important limitation. Identical outlier counts with or without the tracker (`docs/MVP_ABLATION.md §6`).
2. **The intrinsic single-frame classical FPR (44.9%) is unchanged** — unfixable without touching the frozen classical detector algorithm; what changed is the closed-loop consequence, a different, real, measured quantity.
3. **AI-only reacquisition through `resolve_perception()` is not implemented** — the prerequisite gate exists (ADR-019), unlocking it is a distinct, deliberately deferred change.
4. **Coverage cost is real**: Hybrid+Tracker trades ~20 points of coverage for the outlier reduction.
5. Evaluated at n=5 seeds per scenario (matches the frozen protocol) — real, not large-sample, statistics.

## 6. Hardware Boundary

Zero physical camera, beacon, servo, or pan/tilt hardware has been used anywhere in this
project. Every number in this document comes from the deterministic C++ simulation on
an Apple M5 desktop CPU. Mission Control's top bar now says **"Simulation"** explicitly
next to the project name, and its link-status indicator reads "Sim Feed Active/Fault"
(not "Uplink") specifically so it cannot be misread as a live hardware/RF connection.

## 7. Claims SAFE for the SIH PPT

- Real, closed-loop C++20 tracking simulation with a validated PID baseline (7/7 Step-10 gates).
- Real TinyBeaconNet inference in C++ via OpenCV-DNN (not a stub), with measured Python<->C++ numeric parity.
- Real Classical + AI Safe Hybrid fusion policy (ADR-018), safety-gated, no confidence override.
- Real alpha-beta state estimation with a bounded, tested coast/reacquire state machine.
- **99.5% reduction in severe (>50px) closed-loop pointing outliers** from the measured clutter-mitigation fix, reproduced bit-identically in this freeze pass.
- Every configuration fits inside the 20 ms / 50 Hz software timing budget on this development machine (measured, not estimated).
- Real, live engine telemetry in Mission Control (not canned data) — enforced by an automated "no Math.random" guard and a regression test that the engine actually receives the requested mode/tracker flags.
- 17/17 C++ test suites and 20/20 frontend E2E tests passing, reproduced in this freeze pass.

## 8. Claims NOT SAFE for the SIH PPT

- "Tested on real hardware" / any embedded, flight, or physical-gimbal performance claim.
- "Solves clutter false-lock" (unqualified) — it solves spatially/temporally *incoherent* clutter only; say so if asked.
- "Solved moving-distractor / adversarial identity tracking" — explicitly not solved (§5.1).
- "Eliminated the classical false-positive rate" or any claim that 44.9% has been reduced — it has not; a different, closed-loop quantity was reduced instead.
- Any specific numeric claim not traceable to `docs/MVP_METRICS.md`, `docs/MVP_ABLATION.md`, or this document.

## 9. Recommendation

**READY FOR PPT**, contingent on presenting §5/§8 proactively rather than only under
questioning — this system has earned a strict, evidence-backed pitch, not an inflated one.
