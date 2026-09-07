<!--
DRAFT release notes for tag v2_sih_mvp (already exists, points to cc8069e on main).
NOT published. To publish once reviewed:

  gh release create v2_sih_mvp \
    --title "FSOC SIH 2026 Software MVP V2" \
    --notes-file docs/RELEASE_NOTES_v2_sih_mvp.md \
    --target main

(--target is only needed if the tag doesn't already point where you want; it does,
so this can be omitted. Add --draft to review on GitHub before publishing, or
--latest to mark it as the repo's "Latest release".)
-->

## Project identity

FSOC — autonomous closed-loop coarse alignment for mobile Free-Space Optical
Communication (FSO) terminals. Smart India Hackathon 2026, problem statement SIH26169.
A deterministic C++20 simulation of the full SEE → ESTIMATE → PREDICT → CORRECT loop:
synthetic camera → classical + neural (TinyBeaconNet) perception → Safe Hybrid fusion →
alpha-beta state estimation → PID pan/tilt control.

## Major capabilities

- Closed-loop pan/tilt tracking simulation with a validated PID baseline (`v1_baseline`).
- Real TinyBeaconNet CNN (27,282 parameters) trained on a seeded synthetic dataset,
  exported to ONNX, and run natively in C++ via OpenCV-DNN — no Python in the runtime path.
- Safe Hybrid fusion policy (ADR-018): classical + AI cross-validation with unconditional
  rejection on disagreement — no confidence overrides.
- `TargetTracker`: a minimal alpha-beta state estimator with a temporal-consistency gate
  and a bounded coast/reacquire state machine (ADR-019) — deliberately not a Kalman/UKF.
- 5 named, deterministic demo presets (`normal`/`noise`/`occlusion`/`clutter`/`reacquisition`).
- Next.js Mission Control frontend with live (ENGINE) and deterministic-replay (REPLAY)
  telemetry modes, and a real-time "STATE ESTIMATOR" diagnostic panel.
- GitHub Actions CI (C++ build+test, frontend typecheck/lint/build/E2E) on every push/PR.

## Verified metrics

All measured by committed, deterministic tools in this repository (`docs/MVP_METRICS.md`,
`docs/MVP_ABLATION.md`, `docs/SIH_MVP_FREEZE.md`):

- Step-10 baseline acceptance: **7/7 PASS**
- C++ test suites: **17/17** (100%)
- Frontend end-to-end tests: **20/20**
- Severe (>50px) closed-loop outliers: Classical 2,240 → Classical+Tracker 12;
  Hybrid 1,808 → Hybrid+Tracker 9 (**~99.5% reduction** in both cases)
- Full-step latency, Hybrid+Tracker, P95: **~1.2 ms** against a 20 ms / 50 Hz budget
  (development-machine CPU, not a hardware claim)
- Telemetry: 42-column real CSV export

## Demo commands

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
./build/debug/fsoc_demo normal
./build/debug/fsoc_demo clutter
./build/debug/fsoc_demo occlusion
./build/debug/fsoc_demo reacquisition
cd frontend && npm install && npm run dev   # http://localhost:4317
```

Full 15-step judge walkthrough: `docs/MVP_GOLDEN_DEMO.md`.

## Known limitations

- A temporally coherent (smoothly moving) distractor defeats the clutter mitigation
  completely — a disclosed, currently-unresolved gap.
- The intrinsic single-frame classical false-positive rate (44.9%) is unchanged;
  unfixable without modifying the frozen classical detector algorithm.
- AI-only reacquisition through the fusion policy is not implemented (deliberately
  deferred, ADR-019).
- Real coverage cost: the clutter mitigation trades ~20 points of coverage for the
  outlier reduction above.

## Hardware boundary

Zero physical camera, beacon, servo, or pan/tilt hardware has been used anywhere in
this project. Every metric above is from the deterministic C++ simulation on a
development machine. No embedded/flight/hardware real-time performance claim is made.

## Full details

`docs/SIH_MVP_FREEZE.md` is the canonical source for this release's build/test status,
safe vs. unsafe claims for presentation, and exact reproduction commands.
