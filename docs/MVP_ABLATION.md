# MVP Ablation: TargetTracker vs. the Stage-4 clutter false-lock weakness

Covers MVP-V2 completion-loop **Phase E** (clutter false-lock investigation), **Phase F**
(Hybrid V2 policy), and **Phase H** (Classical / Hybrid / Hybrid+estimator ablation). See
`DECISIONS.md` ADR-019 for the architecture-decision record and `docs/19 §9` for the ADR-018
deferral this closes. Every number below is measured by a committed, deterministic tool
(`apps/stage4_tracker_ablation.cpp`) on this development machine — none is estimated.

## 1. Starting point (what this investigates)

`docs/MVP_METRICS.md §2` reported, from the frozen Stage-4 protocol
(`docs/21_AI_STAGE4_EVALUATION_PROTOCOL.md`): Classical's naive "brightest connected component"
rule false-locks onto bright clutter far more than expected in isolation — 44.9% common-frame
false-positive rate, 2,240 severe (`>50px`) closed-loop control-pointing outliers, up to 708px
error — and Safe Hybrid (ADR-018) only partially mitigates this (≈19.3% fewer severe outliers)
because it inherits Classical's raw output whenever AI abstains, which is the majority of frames
(AI recall ≈16-40% depending on evaluation).

The task: investigate *why*, at a candidate-evidence level, and implement the smallest
generalizable mitigation — without touching the classical detector algorithm, `resolve_perception()`,
PID gains, or `v1_baseline` (all explicitly frozen).

## 2. Method

`apps/stage4_tracker_ablation.cpp` is a new, additive evaluation tool (it does not modify
`stage4_evaluation.cpp` or the frozen Stage-4 report). It reuses the exact frozen scenarios
(`fsoc::stage4::kAllScenarios`, 11), seeds (`kStage4BaseSeeds`, 5), and closed-loop duration
(8s @ 50Hz = 400 frames/seed) as the frozen protocol, driving the same unmodified
`BeaconDetector` / `AiBeaconDetector` / `resolve_perception()` / PID, across four configurations:

| config | perception | tracker (`fsoc::TargetTracker`) |
|---|---|---|
| **A. CLASSICAL** | `PerceptionMode::Classical` | off (= frozen Stage-4 Classical row, bit-identical) |
| **B. CLASSICAL+TRACKER** | `PerceptionMode::Classical` | on, default config |
| **C. HYBRID** | `PerceptionMode::Hybrid` (ADR-018) | off (= frozen Stage-4 Hybrid row, bit-identical) |
| **D. HYBRID+TRACKER (Hybrid V2)** | `PerceptionMode::Hybrid` | on, default config |

The tracker, when "on," is layered strictly **after** `resolve_perception()` — exactly how
`fsoc::SimulationRunner::step()` wires it (`include/fsoc/target_tracker.hpp`,
`tracker_min_confidence_to_steer = 0.4`, the same default `SimulationRunner` uses). Rows A and C
reproduce the frozen protocol's own Classical/Hybrid closed-loop numbers exactly (2,240 / 1,808
severe outliers — see §4), which is itself a correctness check on this new tool.

Reproduce: `cmake --build --preset release --target stage4_tracker_ablation && ./build/release/stage4_tracker_ablation --out generated/ai_stage4_ablation` (~15 min on this machine; `--quick` for a fast smoke run, clearly not the frozen counts).

## 3. Root cause (Phase E investigation)

First attempt (an early, unreleased version of the gate) made `BrightDistractor` *worse*, not
better: enabling the tracker dropped coverage hard but did **not** eliminate severe outliers.
Investigating why, at the candidate level:

- `stage4_degradation.cpp apply_clutter()` places the distractor at a **uniformly random position
  across the whole frame**, independent of the beacon, **re-rolled every single frame** (seeded
  by `frame_seed(scenario, base_seed, frame_index)`).
- `BrightDistractor`'s distractor is present with probability 1.0 and peaks at 230-255 — brighter
  than the beacon on most frames — so Classical's "select the brightest connected component" rule
  picks the distractor, not the beacon, on the large majority of frames, **including the first
  few acquisition frames**.
- `TargetTracker`'s original acquisition logic confirmed `Acquiring -> Tracking` after
  `acquire_frames_required` (default 3) *consecutive presences*, with no check that those 3
  measurements agreed with each other. Three consecutive detections of three different,
  unrelated clutter positions confirmed a "Tracking" lock just as readily as three consistent
  real-beacon detections would have.
- Once confirmed on a bad (clutter) anchor, the *established*-track outlier gate then rejected
  the real beacon as an implausible jump relative to that garbage anchor — the mitigation was
  gating out the correct signal, not the incorrect one.

**Fix** (`TargetTracker::begin_acquisition`, `src/target_tracker.cpp`): acquisition now also
checks each new candidate against `outlier_gate_px` (the same gate an established track uses). A
candidate that lands far from the acquisition already in progress **restarts** acquisition fresh
at that new position, rather than counting toward confirmation. This directly targets the
observed failure (spatially incoherent "confirmation") without touching the classical detector,
`resolve_perception()`, or any evaluation-specific position — the gate is a fixed radius derived
from Step-10 motion evidence (`target_tracker.hpp`), not tuned against this benchmark's results.
All 18 pre-existing `TargetTracker` unit tests pass unmodified after the fix; a new test
(`test_acquisition_restarts_on_spatially_inconsistent_measurements`) covers the new behavior
directly, and `test_closed_loop_tracker_seam` (Stage-4) locks in the improvement.

## 4. Results — full frozen protocol (11 scenarios x 5 seeds x 400 frames = 22,000 frames/config)

### Pooled overall

| config | accepted-detection fraction (coverage) | control outliers `>20px` | `>50px` | `>100px` | max error (px) | RMS angular error |
|---|---|---|---|---|---|---|
| A. CLASSICAL | 99.69% | 2,297 | **2,240** | 2,078 | 581.4 | 3.257° |
| B. CLASSICAL+TRACKER | 77.51% | 63 | **12** | 1 | 127.5 | 0.656° |
| C. HYBRID | 97.35% | 1,852 | **1,808** | 1,663 | 581.4 | 3.116° |
| D. HYBRID+TRACKER (V2) | 77.05% | 55 | **9** | 1 | 106.6 | 0.657° |

(A and C reproduce `docs/MVP_METRICS.md`'s frozen closed-loop numbers exactly — 2,240 and 1,808
— confirming this tool measures the same thing the frozen protocol does.)

- **Severe (`>50px`) outliers fall 99.46%** (Classical -> Classical+Tracker: 2,240 -> 12) and
  **99.50%** (Hybrid -> Hybrid V2: 1,808 -> 9).
- **Coverage cost:** -22.2 points (Classical) / -20.3 points (Hybrid V2) in the pooled overall.
- **RMS angular error drops ~80%** (3.26° -> 0.66°) — but this is partly a *survivorship* effect,
  not purely "more accurate tracking": RMS is computed only over frames the system claims a
  detection for, and the tracker's whole mechanism is to stop claiming detection on the frames
  it can't trust. Read it as "when the system reports a detection, that detection is now far more
  trustworthy" — not "the same detections are 5x more precise."

### Where the effect comes from

**2 of 11 scenarios (`BrightDistractor`, `AdversarialAmbiguity`) account for 100% of Classical's
severe outliers** (1,281 + 959 = 2,240, exactly the pooled total) — the other 9 scenarios already
show zero `>50px` outliers even without any mitigation.

| scenario | config | coverage | `>50px` outliers | max err (px) |
|---|---|---|---|---|
| E. BRIGHT_DISTRACTOR | CLASSICAL | 100.0% | 1,281 | 581.4 |
| E. BRIGHT_DISTRACTOR | CLASSICAL+TRACKER | 16.5% | **2** | 77.4 |
| E. BRIGHT_DISTRACTOR | HYBRID | 86.6% | 1,020 | 581.4 |
| E. BRIGHT_DISTRACTOR | HYBRID+TRACKER (V2) | 16.5% | **1** | 50.2 |
| K. ADVERSARIAL_IDENTITY_AMBIGUITY | CLASSICAL | 100.0% | 959 | 483.7 |
| K. ADVERSARIAL_IDENTITY_AMBIGUITY | CLASSICAL+TRACKER | 39.8% | **10** | 127.5 |
| K. ADVERSARIAL_IDENTITY_AMBIGUITY | HYBRID | 87.6% | 788 | 483.7 |
| K. ADVERSARIAL_IDENTITY_AMBIGUITY | HYBRID+TRACKER (V2) | 34.9% | **8** | 106.6 |

For these two scenarios specifically, the mitigation trades ~60-84 points of coverage for a
~99% cut in severe pointing errors — an honest, real trade, not a free win. `BrightDistractor` in
particular is a deliberately adversarial stress case (distractor present with probability 1.0,
brighter than the beacon on most frames); a coverage floor near 16-17% there reflects how rarely
Classical's own raw output is trustworthy enough, 3 frames in a row, to acquire on.

**The 9 non-adversarial scenarios** (`Clean`, `LowSnr`, `StarClutter`, `HotPixels`, `Blur`,
`MotionBlur`, `BackgroundGradient`, `MixedRandomized`, and — differently, see below —
`TargetAbsent`) show **zero change in outlier counts** and only a ~0.5-percentage-point coverage
cost (99.5% vs 100%), matching exactly the one-time 3-frame acquisition ramp at the start of each
400-frame run. The mitigation does not cost anything meaningful in the common case.

**`TargetAbsent` is a distinct, important case.** Classical (with or without Hybrid) reports a
detection 96.6% of the time **when there is no target at all** (RMS error 7.3° — it is
confidently pointing at background noise). With the tracker, coverage drops to 0.3%: the
consistency requirement correctly recognizes that a genuinely absent target produces no
temporally coherent candidate, and the system (correctly) reports "no detection" essentially
100% of the time instead of confidently pointing at noise. This is a safety improvement, not a
coverage regression.

## 5. What this does NOT fix

- **Classical's single-frame FPR is unchanged (44.9%, common-frame benchmark).** That number is
  intrinsic to a single, isolated frame with no temporal context — there is nothing for a
  tracker to gate against on frame one, and the classical detector algorithm itself is frozen
  (explicit constraint). The fix operates on the closed-loop *consequence* of that intrinsic
  weakness, not the weakness itself.
- **Coverage cost is real and scenario-dependent**, not a free correctness win — reported above
  without rounding it away.
- **AI-only reacquisition through `resolve_perception()` is still not implemented** (ADR-018
  case 3 unchanged) — see ADR-019 and `docs/19 §9`. The gate built here filters what
  `resolve_perception()` already outputs; it does not yet let a tracker-consistent AI-only
  candidate through the frozen fusion policy itself.

## 6. Recommendation

Enable the tracker (`tracker_enabled = true`) whenever pointing-error safety matters more than
raw coverage — the two adversarial Stage-4 scenarios are a reasonable proxy for "clutter is
plausibly present." Leave it off (current default) for the common/clean case, where it adds a
small, one-time acquisition-ramp cost for zero benefit. For the golden demo (`docs/17_DEMO_FREEZE.md`
successor), this maps naturally onto a disturbance-scenario toggle: NORMAL runs classical
default; a CLUTTER disturbance is the natural place to demonstrate Hybrid V2 visibly
recovering from a false lock rather than steering into one.

## 7. Ablation A/B/C (ties to a canonical estimator/prediction contribution, Phase H)

Restated against the phase-H framing (does adding the estimator/prediction layer on top of
Hybrid actually help, isolated from the other two variables):

- **A. Classical only** = row A above.
- **B. Classical + AI Hybrid** = row C above (ADR-018, unchanged).
- **C. Classical + AI + estimator/prediction** = row D above (Hybrid V2).

A -> B (Hybrid alone) already cuts severe outliers 2,240 -> 1,808 (19.3%, matching
`docs/MVP_METRICS.md`'s original headline). B -> C (adding the estimator) cuts the remaining
1,808 down to 9 — a further 99.5% reduction, an order of magnitude larger effect than Hybrid
alone — at the coverage cost detailed in §4. The estimator/prediction layer, not the Classical/AI
fusion policy, is what actually neutralizes this specific failure mode.
