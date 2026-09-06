# Golden Demo V2 (MVP-V2 Phase K)

A specific, reproducible walkthrough for an SIH judge session. Every step runs a real
command against the real system (no scripted/staged data); the corresponding `docs/`
evidence file is cited so a skeptical judge can independently verify every claim made
live. Total time: ~10 minutes including build.

## Prerequisites (once)

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
cmake --preset release && cmake --build --preset release
cd frontend && npm install && npm run build && cd ..
```

## The 15 steps

**1. Prove it's a real, tested system, not a slideshow.**
```bash
ctest --preset debug
```
Expect `100% tests passed out of 17`. This is the whole C++ test suite (target tracker,
perception, telemetry, demo packaging, Stage-4 determinism) running live, not a
pre-recorded result.

**2. Baseline: the calm case.**
```bash
./build/debug/fsoc_demo normal
```
Watch the live status line converge to `angErr` near 0° and stay there. `lock=TRACKING
conf=1.00` throughout. This is Classical, tracker off — the frozen `v1_baseline`.

**3. Launch Mission Control and switch to LOCAL ENGINE mode.**
```bash
cd frontend && npm run dev
```
Open the app, select the **ENGINE** source toggle (not REPLAY). Point out: this pulls
live telemetry from the same `fsoc_demo` binary just run in step 2 — not a canned
fixture. (`docs/18_FRONTEND_DATA_CONTRACT.md`; enforced by the repo's own Playwright
test that fails the build if any application source calls `Math.random`.)

**4. Show ordinary sensor noise doesn't need help.**
```bash
./build/debug/fsoc_demo noise
```
Detection stays ~100% with Classical alone, tracker off. This matches the frozen
Stage-4 `LowSnr` scenario result (`docs/MVP_METRICS.md §2`) — noise alone was never
the weak point.

**5. Show the actual, honest problem: clutter false-lock.**
```bash
./build/debug/fsoc_demo clutter
```
Watch the live status line: `TARGET_LOST`, `lock=ACQUIRING`/`SEARCHING` cycling,
`rejected=DETECTOR_DISAGREEMENT`. This preset already runs Hybrid+Tracker (the fix is
active) — say so explicitly, then cite the *raw Classical* number from
`docs/MVP_ABLATION.md §4`: **2,240 severe (>50px) pointing errors** across the frozen
Stage-4 protocol when this same failure mode runs completely unmitigated. The live demo
shows one instance; the ablation doc shows the statistics behind it.

**6. Show the fix quantitatively, not just anecdotally.**
Open `docs/MVP_ABLATION.md §4`. Point at the table: Hybrid+Tracker cuts severe outliers
from 1,808 to 9 (**99.5% reduction**) at a real, disclosed coverage cost (~20 points).
Say the honest part out loud: this is a trade, not a free win, and it is reported as
such.

**7. Show the limitation — don't let the fix look like magic.**
Open `docs/MVP_ABLATION.md §6` (`MOVING_DISTRACTOR`). A *temporally coherent* moving
distractor defeats the same gate completely (identical 65 outliers with or without the
tracker). Say plainly: the mitigation defends against spatially-incoherent clutter, not
any adversarial candidate. This is the single most important "don't oversell it"
moment in the demo.

**8. Show short-gap bridging.**
```bash
./build/debug/fsoc_demo occlusion
```
Watch `lock=TRACKING` → `lock=COASTING conf=0.50 [PREDICTED]` → `lock=TRACKING` again,
with no `TARGET_LOST` in between. The system predicted through a real 2-frame gap
instead of dropping the track.

**9. Show the full state machine, including honest failure.**
```bash
./build/debug/fsoc_demo reacquisition
```
Watch `TRACKING` → `COASTING` (confidence decaying) → a `TARGET_LOST`/`SEARCHING`
window (the gap exceeded the coast horizon — this is supposed to happen) → a fresh
`ACQUIRING` → `TRACKING`. Say explicitly: **Lost is a correct outcome for a long enough
gap** — the system does not pretend to track through anything indefinitely.

**10. Tie it back to Mission Control's live diagnostic panel.**
In the frontend (still running from step 3), the "STATE ESTIMATOR (P0-v2)" panel in the
Telemetry Stream rail shows the same `LOCK STATE` / `CONFIDENCE` / `MEASURED`↔`PREDICTED`
values the CLI just printed, for a live engine-mode run with the tracker enabled
(`?mode=hybrid&tracker=1` on the simulation API — `docs/18`).

**11. Show the audit trail.**
```bash
./build/debug/fsoc_demo clutter --csv /tmp/clutter_run.csv --quiet
head -1 /tmp/clutter_run.csv | tr ',' '\n' | wc -l   # 42
```
Every frame of every run can be exported as a 42-column CSV
(`docs/08_TELEMETRY_SCHEMA.md`) — truth, measurement, perception diagnostics, and
tracker diagnostics side by side, for offline scrutiny.

**12. Show the ablation, not just the headline.**
Open `docs/MVP_ABLATION.md §8`: A (Classical) → B (+ Hybrid fusion, 19.3% fewer severe
outliers) → C (+ estimator/prediction, a further 99.5% reduction) — the estimator, not
the classical/AI fusion policy, is what actually neutralizes this specific failure mode.
Say which piece did the work; don't credit the wrong component.

**13. Show it fits the real-time budget.**
Open `docs/MVP_METRICS.md §5`. Every configuration, including the most expensive
(Hybrid+Tracker), stays under 8.2% of the 20 ms / 50 Hz budget at P95 on this
development machine. State the boundary out loud: **development-machine CPU
performance, not embedded/flight hardware** — no physical camera or gimbal has been
used anywhere in this project.

**14. Show the frontend is held to the same honesty standard.**
```bash
cd frontend && npx playwright test
```
20/20 pass, including a guard that fails the build if any application source contains
`Math.random` (`tests/e2e/smoke.spec.ts`) and a regression test that the engine-mode
`--tracker`/`--mode` flags actually reach the C++ binary (the exact bug this session
found and fixed while wiring Phase I).

**15. Close on the honest state of the project.**
State plainly, from `docs/MVP_ABLATION.md` and the FSOC — MVP V2 FINAL REPORT: what
works (state estimation, short-gap bridging, large false-lock reduction, real-time
headroom), what doesn't yet (moving/coherent clutter, AI-only reacquisition still
unimplemented, single-frame classical FPR unchanged), and the hardware boundary
(simulation only). End with the GO/NO-GO recommendation from that report, not an
inflated summary.

## Reproducing steps 2/4/5/8/9 back-to-back (regression check for this doc itself)

```bash
for p in normal noise clutter occlusion reacquisition; do
  echo "=== $p ==="; ./build/debug/fsoc_demo "$p" --quiet
done
```
All five must exit 0 and print a summary block; this is re-verified whenever
`apps/fsoc_demo.cpp` or `src/demo.cpp` changes (see `tests/step11_tests.cpp`
`test_all_disturbance_presets_run_end_to_end`).
