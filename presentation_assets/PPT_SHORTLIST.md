# FSOC — SIH 2026 PPT Shortlist

Ranked from all 17 real captured assets. "I would rather receive 8 exceptional, truthful
screenshots than 30 mediocre ones" — this list picks the 8 that carry the deck. Every image
below was opened and visually verified before being included (see the final evidence-pack
report for what that review caught and fixed).

Paths are relative to `presentation_assets/`.

## MUST USE (5)

### 1. `screenshots/01_mission_control_tracking.png`
- **Slide:** Opening / hero
- **Proves:** A real, working, closed-loop tracking UI exists — not a mockup or slide deck. Live telemetry rail, real crosshair-on-beacon lock.
- **Does NOT prove:** Accuracy, robustness, or AI capability — it's a single mid-run frame. Pair with metrics/ablation for that.

### 2. `diagrams/16_system_story.png`
- **Slide:** "How FSOC works" (early, right after the problem statement)
- **Proves:** The SEE → ESTIMATE → PREDICT → CORRECT closed loop is the organizing concept, and each stage maps to real implemented code.
- **Does NOT prove:** That any specific stage works well — it's a concept diagram, not measured evidence. Follow immediately with 04 and 19 as proof.

### 3. `screenshots/04_hybrid_perception.png`
- **Slide:** AI / Hybrid perception + state estimation
- **Proves:** The Safe Hybrid fusion (Classical + TinyBeaconNet) and the alpha-beta state estimator are both real, running, and populated by live engine data in the same frame — not two separate demos stitched together.
- **Does NOT prove:** AI accuracy in isolation — the panel shows fusion output and estimator state, not a raw model benchmark (that's `metrics/10_metrics_summary.png`'s AI-latency card). Also disclose: reached via route interception on the app's already-shipped API (no UI toggle exists yet) — see `manifest.json` caveat.

### 4. `diagrams/19_failure_recovery.png`
- **Slide:** Robustness / failure recovery
- **Proves:** The system detects target loss, coasts on a predicted position instead of losing lock outright, and reacquires — all three frames from one deterministic run, so the sequence is a real observed trajectory, not assembled from unrelated moments.
- **Does NOT prove:** That every occlusion is survivable — this is one scenario/seed. The ablation table (`metrics/11_ablation.png`) gives the aggregate, disclosed picture including where this mitigation still fails (a coherent moving distractor).

### 5. `metrics/10_metrics_summary.png`
- **Slide:** Measured results
- **Proves:** Quantitative, reproducible numbers — test pass rates, the ~99.5% severe-outlier reduction, real AI inference latency — all sourced from a committed doc, not invented for the deck.
- **Does NOT prove:** Real-world / hardware performance — explicitly labeled "SIMULATION / DEVELOPMENT-MACHINE RESULTS" on the graphic itself. State that label out loud when presenting.

## STRONG OPTIONAL (3)

### 6. `metrics/11_ablation.png`
- **Slide:** Why the state estimator matters (mechanism slide, follows #4)
- **Proves:** The estimator — not the Classical/AI fusion choice — is what neutralizes the severe-outlier failure mode (A→B and C→D both drop ~99.5%). Also proves intellectual honesty: the footer discloses the coverage cost and the still-unsolved clutter case.
- **Does NOT prove:** That coverage cost is free — 77% vs 99.7% coverage is a real, disclosed tradeoff; don't let a judge assume otherwise.

### 7. `diagrams/18_before_after.png`
- **Slide:** Convergence proof (compact 2-panel alternative if slide count is tight)
- **Proves:** The same PID loop that appears in 02/03 individually, in one glance.
- **Does NOT prove:** Anything beyond what 02+03 already show — use this OR the pair, not both, to avoid redundancy.

### 8. `screenshots/12_validation_terminal.png`
- **Slide:** Engineering rigor / "this is tested" slide
- **Proves:** 17/17 CTest suites and 7/7 Step-10 baseline-acceptance scenarios genuinely pass — real terminal output, not retyped text.
- **Does NOT prove:** Coverage completeness — passing tests confirm the tested behaviors are correct, not that all behaviors are tested.

## TECHNICAL BACKUP (use only if a judge asks a follow-up question)

| File | Use when a judge asks... |
|---|---|
| `diagrams/15_architecture.png` | "What's the actual system architecture / module breakdown?" |
| `screenshots/13_hybrid_cli_demo.png` | "Does this run outside the browser / can I see the raw engine output?" |
| `screenshots/08_error_convergence.png` | "Can I see all the telemetry channels, not just the summary?" |
| `screenshots/09_perception_frame.png` | "What does the raw simulated camera frame look like?" |
| `screenshots/02_initial_misalignment.png` / `03_alignment_converged.png` | Same as 18, if shown individually instead of as a collage |
| `screenshots/05_occlusion_coasting.png` / `06_reacquisition.png` / `07_loss_scenario_tracking.png` | Same as 19, if shown individually instead of as a collage |

## Explicitly not shown truthfully

`screenshots/07_clutter_rejection.png` could not be produced (no live UI route for the
clutter-distractor scenario) and is documented as BLOCKED in `manifest.json` rather than
faked. If a judge asks about it directly, answer with `metrics/11_ablation.png`'s own
disclosure and the CLI command in `docs/MVP_GOLDEN_DEMO.md` (`fsoc_demo clutter --mode hybrid
--tracker`) — this is a case where the honest answer is a real command, not a real screenshot.
