# Documentation Index

A reviewer's map through this repository's docs. Start at the top and go as deep as you
need — nothing here duplicates another file; each doc is the single canonical source
for what it covers.

## Start here

| doc | what it's for |
|---|---|
| [`SIH_MVP_FREEZE.md`](SIH_MVP_FREEZE.md) | The frozen SIH MVP state: build/test status, measured metrics, safe vs. unsafe claims for a judge presentation, exact demo commands |
| [`MVP_GOLDEN_DEMO.md`](MVP_GOLDEN_DEMO.md) | The 15-step, fully reproducible judge-session walkthrough |
| [`RELEASE_NOTES_v2_sih_mvp.md`](RELEASE_NOTES_v2_sih_mvp.md) | Draft GitHub release notes for the `v2_sih_mvp` tag (not yet published) |
| [`PHONE_CAMERA_METRICS.md`](PHONE_CAMERA_METRICS.md) | Mobile Phone Camera-in-the-Loop: architecture, JSON telemetry schema, claim boundary, real-camera metrics |
| [`PHONE_CAMERA_GOLDEN_DEMO.md`](PHONE_CAMERA_GOLDEN_DEMO.md) | The 15-step real-camera demo walkthrough (run yourself — needs a real camera) |
| [`PHONE_CAMERA_TEST_PLAN.md`](PHONE_CAMERA_TEST_PLAN.md) | Automated (CTest) vs. manual (hardware-required) test split, with a PASS/FAIL checklist |

## Architecture

| doc | what it's for |
|---|---|
| [`19_AI_PERCEPTION_ARCHITECTURE.md`](19_AI_PERCEPTION_ARCHITECTURE.md) | Classical + AI + Safe Hybrid fusion design (ADR-015 through ADR-018) |
| [`09_FUTURE_ARCHITECTURE.md`](09_FUTURE_ARCHITECTURE.md) | The swappable-interface boundary for a future real-hardware port |
| [`15_INTERFACE_CONTRACTS.md`](15_INTERFACE_CONTRACTS.md) | Frozen module boundaries and data contracts |
| [`18_FRONTEND_DATA_CONTRACT.md`](18_FRONTEND_DATA_CONTRACT.md) | The C++ → frontend `DemoSnapshot` transport shape |
| [`08_TELEMETRY_SCHEMA.md`](08_TELEMETRY_SCHEMA.md) | The 42-column CSV telemetry schema, field by field |
| [`04_COORDINATES_AND_MATH.md`](04_COORDINATES_AND_MATH.md) | The frozen world/camera/image coordinate conventions |

## Validation

| doc | what it's for |
|---|---|
| [`MVP_METRICS.md`](MVP_METRICS.md) | Consolidated real measured numbers across every stage, including the full latency budget |
| [`MVP_ABLATION.md`](MVP_ABLATION.md) | The clutter false-lock investigation, the state-estimator mitigation, and the A/B/C ablation, all measured |
| [`21_AI_STAGE4_EVALUATION_PROTOCOL.md`](21_AI_STAGE4_EVALUATION_PROTOCOL.md) | The frozen Classical/AI/Hybrid evaluation protocol |
| [`16_BASELINE_ACCEPTANCE.md`](16_BASELINE_ACCEPTANCE.md) | The 7 frozen Step-10 baseline acceptance gates |
| [`07_TEST_AND_VALIDATION_PLAN.md`](07_TEST_AND_VALIDATION_PLAN.md) | The original test/validation strategy |

## Design decisions

| doc | what it's for |
|---|---|
| [`../DECISIONS.md`](../DECISIONS.md) | Every architecture decision (ADR-001 through ADR-019) with the evidence behind it |

## Development history

| doc | what it's for |
|---|---|
| [`DEVELOPMENT_HISTORY.md`](DEVELOPMENT_HISTORY.md) | The chronological Step 1 → Step 11 build log (moved out of `README.md` to keep the landing page judge-facing) |
| [`archive/`](archive/) | Superseded, purely historical bookkeeping from the original Python→C++ starter-kit conversion |

## Everything else

`00_PROJECT_BRIEF.md` / `01_PRD.md` / `02_SRS.md` / `03_TECHNICAL_DESIGN.md` /
`05_48_HOUR_ROADMAP.md` / `06_DEFINITION_OF_DONE.md` / `10_DEMO_AND_JUDGING_STORY.md` /
`11_RISK_REGISTER.md` / `12_EXPERIMENT_PROTOCOL.md` / `13_GIT_WORKFLOW.md` /
`14_TASK_BOARD.md` / `16_AI_CODING_GUARDRAILS.md` / `17_CLAUDE_CODE_USAGE.md` /
`17_DEMO_FREEZE.md` / `20_AI_DATASET_AND_TRAINING.md` / `09_VISUALIZATION.md` — the
original planning/process docs from each build phase. Still accurate for their scope;
not duplicated or re-summarized here.
