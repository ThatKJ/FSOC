# FSOC — SIH 2026 Visual Evidence Pack

This directory is the judge-facing evidence pack for the FSOC (SIH26169) software MVP. Every
screenshot is either **(A)** the real Mission Control frontend driving the real `fsoc_demo`
C++ engine (`ENGINE` mode), **(B)** a deterministic pre-generated asset from the repository
(`REPLAY`), or **(C)** a graphic generated from real, committed measurement docs
(`docs/MVP_METRICS.md`, `docs/MVP_ABLATION.md`). No telemetry value, chart, or number in this
pack is fabricated, hand-edited, or randomly generated. See `manifest.json` for the full
machine-readable record (one entry per asset, including the two items that could not be
truthfully produced).

Captured at commit `9f286f4` (`main`, 2026-09-07), simulation only — no physical camera,
beacon, or pan/tilt hardware exists in this repository.

Regenerate with:

```bash
cd frontend
npm run build && npm run start &      # Mission Control on :4317
FSOC_COMMIT_SHA=$(git rev-parse --short HEAD) node scripts/capture-sih-assets.mjs
```

## Screenshots (`screenshots/`)

| # | File | Shows | Scenario | Data source | Mode |
|---|------|-------|----------|-------------|------|
| 01 | `01_mission_control_tracking.png` | Hero: Mission Control actively tracking mid-run | Static Acquisition | Live engine response | ENGINE |
| 02 | `02_initial_misalignment.png` | Real large initial pointing error before convergence | Static Acquisition | Live engine response | ENGINE |
| 03 | `03_alignment_converged.png` | Same run, converged near zero error | Static Acquisition | Live engine response | ENGINE |
| 04 | `04_hybrid_perception.png` | Hybrid fusion + state estimator panels, real TRACKING lock | Static Acquisition | Live engine response | ENGINE (`mode=hybrid&tracker=1`) |
| 07 | `07_loss_scenario_tracking.png` | Same loss-scenario run as 05/06, early TRACKING frame (collage support asset) | Target Loss & Re-entry | Live engine response | ENGINE (`mode=hybrid&tracker=1`) |
| 05 | `05_occlusion_coasting.png` | Detection gap bridged by the state estimator (COASTING, PREDICTED) | Target Loss & Re-entry | Live engine response | ENGINE (`mode=hybrid&tracker=1`) |
| 06 | `06_reacquisition.png` | Reacquisition after target loss, fresh TRACKING lock | Target Loss & Re-entry | Live engine response | ENGINE (`mode=hybrid&tracker=1`) |
| 08 | `08_error_convergence.png` | Full telemetry dashboard, all charts high→low convergence | Static Acquisition | Live engine response | ENGINE (clean baseline) |
| 09 | `09_perception_frame.png` | Real rendered simulator frame, beacon + crosshair | Static Acquisition | `frontend/public/demo/frame-static.png` | REPLAY (Step-9 visualizer) |
| 12 | `12_validation_terminal.png` | Real terminal output: 17/17 CTest, 7/7 Step-10 PASS | n/a | Real stdout, styled not retyped | n/a |
| 13 | `13_hybrid_cli_demo.png` | Real CLI run: Hybrid + tracker, live lock-state transitions | Static Acquisition | Real stdout, styled not retyped | n/a |

`07_clutter_rejection.png` was planned but is **BLOCKED** — no frontend UI route triggers the
clutter-distractor scenario live (it's CLI/Stage-4-only). `14_frontend_validation.png` is
**SKIPPED** (deprioritized, already covered by the CI badge + 12). See `manifest.json` for the
full reasoning on both.

## Metrics (`metrics/`)

| File | Shows | Data source |
|------|-------|-------------|
| `10_metrics_summary.png` | Consolidated real measured metrics (Step-10, CTest, Playwright, Stage-4 outliers, AI latency) | `docs/MVP_METRICS.md` |
| `11_ablation.png` | A/B/C/D ablation table, real measured numbers | `docs/MVP_ABLATION.md` |

## Diagrams (`diagrams/`)

| File | Shows | Data source |
|------|-------|-------------|
| `15_architecture.png` | Implemented-only system architecture, sim vs. future-hardware split | README.md architecture diagram |
| `16_system_story.png` | SEE → ESTIMATE → PREDICT → CORRECT storytelling graphic | Current implementation |
| `18_before_after.png` | Before/after collage from real screenshots 02+03 | `02_initial_misalignment.png` + `03_alignment_converged.png` |
| `19_failure_recovery.png` | 3-panel failure-recovery collage, all panels from the SAME loss-scenario run | `07_loss_scenario_tracking.png` + `05_occlusion_coasting.png` + `06_reacquisition.png` |

## Simulation / hardware boundary

**Every asset in this pack is simulation-only.** No physical camera, optical beacon, pan/tilt
gimbal, or free-space optical link exists in this repository. "ENGINE" means the real C++
`fsoc_demo` simulation binary; it does not mean physical hardware. See
`docs/SIH_MVP_FREEZE.md` for the full hardware-boundary statement.

## Full per-asset detail

See `manifest.json` for the complete record per asset: purpose, scenario, exact
command/route, data source, ENGINE-or-REPLAY, commit SHA, capture date,
simulated-or-hardware, recommended slide, and caveats. See `PPT_SHORTLIST.md` for the
ranked best-6-8 assets for the SIH presentation deck.
