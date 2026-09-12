# FSOC — SIH 2026 PPT Shortlist

Ranked from all 18 real captured assets. "I would rather receive 8 exceptional, truthful
screenshots than 30 mediocre ones" — this list picks the 8 that carry the deck. Every image
below was opened and visually verified before being included (see the final evidence-pack
report for what that review caught and fixed).

Paths are relative to `presentation_assets/`.

## MUST USE (5)

### 1. `screenshots/01_mission_control_tracking.png`
- **Slide:** Opening / hero
- **Proves:** A real, working, closed-loop tracking UI exists — not a mockup. Captured after convergence (pointing error 0.0000°) with Hybrid perception (MODE: HYBRID) and the alpha-beta state estimator (LOCK STATE: TRACKING, CONFIDENCE 1.00) both visibly active in the same frame.
- **Does NOT prove:** AI accuracy or robustness in isolation — it's a single converged frame. Pair with `20_open_vs_closed_loop.png` and `19_failure_recovery.png` for that. Note: PERCEPTION SOURCE reads CLASSICAL for this specific frame — that's the Safe Hybrid fusion's real, safety-gated per-frame decision, not a UI limitation; be ready to explain it if asked.

### 2. `diagrams/16_system_story.png`
- **Slide:** "How FSOC works" (early, right after the problem statement)
- **Proves:** The SEE → ESTIMATE → PREDICT → CORRECT closed loop is the organizing concept, and each stage maps to real implemented code.
- **Does NOT prove:** That any specific stage works well — it's a concept diagram, not measured evidence. Follow immediately with 01 and 19 as proof.

### 3. `metrics/20_open_vs_closed_loop.png`
- **Slide:** Headline quantitative result
- **Proves:** The single easiest number in the whole pack for a judge to remember — Open Loop 6.4549° RMS → Closed Loop 0.5461° RMS, an 11.8× improvement, same trajectory and sensor model, only the correction loop differs. Sourced from real, committed validation output (`docs/MVP_METRICS.md`, and the actual `step10_validation_smoke` terminal line in `12_validation_terminal.png`).
- **Does NOT prove:** Real-world / hardware performance — explicitly labeled "SIMULATION / DEVELOPMENT-MACHINE RESULTS" on the graphic itself. State that label out loud when presenting.

### 4. `diagrams/18_before_after.png`
- **Slide:** Convergence proof
- **Proves:** The real, on-screen pointing-error change for one closed-loop run — 4.13° (true frame-0 peak) → 0.00° (converged) — with a headline number large enough to read from the back of a room, backed by the two real screenshots underneath it.
- **Does NOT prove:** Anything beyond one scenario/run — pair with `20_open_vs_closed_loop.png` for the aggregate open-vs-closed comparison.

### 5. `diagrams/19_failure_recovery.png`
- **Slide:** Robustness / failure recovery
- **Proves:** The system detects target loss, coasts on a predicted position instead of losing lock outright, and reacquires — all three frames from one deterministic run, so the sequence is a real observed trajectory, not assembled from unrelated moments.
- **Does NOT prove:** That every occlusion is survivable — this is one scenario/seed. `metrics/11_ablation.png` gives the aggregate, disclosed picture including where this mitigation still fails (a coherent moving distractor).

## STRONG OPTIONAL (3)

### 6. `metrics/10_metrics_summary.png`
- **Slide:** Measured results (broader summary, if the deck has room for both 20 and a fuller card grid)
- **Proves:** Quantitative, reproducible numbers beyond the headline RMS stat — test pass rates, severe-outlier reduction, real AI inference latency — all sourced from a committed doc.
- **Does NOT prove:** Real-world / hardware performance — same SIMULATION label caveat as #3.

### 7. `screenshots/04_hybrid_perception.png`
- **Slide:** AI / Hybrid perception + state estimation (technical backup / deep-dive to 01)
- **Proves:** The Safe Hybrid fusion and state estimator panels populated by live engine data, captured mid-run rather than post-convergence.
- **Does NOT prove:** Anything additional beyond what 01 already shows — use this only if the deck has a dedicated AI slide separate from the hero.

### 8. `metrics/11_ablation.png`
- **Slide:** Why the state estimator matters (mechanism slide, follows #5)
- **Proves:** The estimator — not the Classical/AI fusion choice — is what neutralizes the severe-outlier failure mode (A→B and C→D both drop ~99.5%). Also proves intellectual honesty: the footer discloses the coverage cost and the still-unsolved clutter case.
- **Does NOT prove:** That coverage cost is free — 77% vs 99.7% coverage is a real, disclosed tradeoff; don't let a judge assume otherwise.

## TECHNICAL BACKUP (use only if a judge asks a follow-up question)

| File | Use when a judge asks... |
|---|---|
| `diagrams/15_architecture.png` | "What's the actual system architecture / module breakdown?" |
| `screenshots/12_validation_terminal.png` | "Can I see the raw test output, not just a summary graphic?" |
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
