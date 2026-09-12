# Live Camera + Real-Data AI — Task State

Tracks the G0–G6 gates from the live-camera/real-data-AI engineering task (2026-09-12).
This supersedes nothing already frozen: `v1_baseline` (Step 10) and the synthetic-data
Stage 1–4 AI perception work stay frozen and reusable. This task adds a **new, additive**
live-camera path and a **real-data** training/eval track alongside them.

Status values: `BLOCKED` (external dependency) · `IN PROGRESS` · `IMPLEMENTED` (code done,
not yet verified) · `AUTOMATED PASS` (passes an automated test/build) · `PHYSICAL PASS`
(verified against real camera/hardware with evidence) · `FAIL`.

Operator confirmed setup (2026-09-12): laptop webcam + phone-screen-dot beacon (Setup A).
No motorized pan/tilt hardware yet — G6 stays BLOCKED/out-of-scope until hardware exists.

## Gate summary

| Gate | Description | Status |
|---|---|---|
| G0 | Audit and source isolation | AUTOMATED PASS — see `docs/LIVE_DATA_AUDIT.md` |
| G1 | Physical live baseline (real camera → classical tracking → matched frame/telemetry) | IN PROGRESS |
| G2 | Measurement and data collection (recording, annotation, splits) | BLOCKED (on G1) |
| G3 | Real-data AI (train/eval TinyBeaconNet on reviewed real captures) | BLOCKED (on G2) |
| G4 | Native deployment + application integration | BLOCKED (on G3) |
| G5 | Demonstration package (evidence export, manual, repeatable demo) | BLOCKED (on G4) |
| G6 | Physical automatic pointing (motorized hardware) | BLOCKED — no hardware available |

## G0 — Audit and source isolation

- [x] Every live-facing screen/API/provider traced to its actual data source —
      `docs/LIVE_DATA_AUDIT.md` §1. Every "live-looking" page on `main` is fed by
      `/api/simulation/[scenario]` (finite `fsoc_demo` run or checked-in fixture JSON).
      No camera path exists on `main` at all yet.
- [x] Phone-camera branch content verified against the pasted prompt pack's claims —
      `docs/LIVE_DATA_AUDIT.md` §2. Claims were accurate; branch is close to
      production-ready (FrameSource abstraction, honest VirtualPanTiltActuator,
      LiveTrackingSession reusing the frozen detector/hybrid/tracker pipeline,
      graceful disconnect handling). Never merged into `main`.
- [x] TinyBeaconNet actual I/O contract verified from model metadata —
      `docs/LIVE_DATA_AUDIT.md` §3. 640×480 reference → 320×240 net input
      `[1,1,240,320]`, heatmap `[1,1,60,80]`, synthetic-only training confirmed.
- [x] Simulation/replay fallback identified — there is no existing live route to
      patch; G1 must build the live boundary so it never touches
      `/api/simulation`, not retrofit a fallback-disable switch.

## G1 — Physical live baseline

In progress. Plan (per audit §2 recommendation): create a local integration branch off
`main`, merge in `feat/phone-camera-in-loop`'s C++ live-camera modules and frontend
live route, resolve conflicts against `main`'s README/design-system/smoke-test drift,
fix the frame↔telemetry identity/atomicity gap (frame-keyed immutable files + atomic
rename + manifest, per pack §E, reusing the existing honest polling transport rather
than introducing a WebSocket server), build, and run all existing + new tests.

## G2 — G6

Not started.

## Engineering assumptions log

- Reusing existing frozen interfaces wherever possible: `fsoc::BeaconDetection` contract,
  `PerceptionMode`/`AiBeaconDetector`/Safe-Hybrid policy, `fsoc::TargetTracker`, the 34/42
  column telemetry schema. A live camera path is a new `FrameSource` implementation, not a
  new detector/controller/telemetry format.
- Actuator stays `NONE`/`MANUAL ASSIST` for the live path until real motor hardware is
  provided (G6). No invented pan/tilt angles.
- Python stays confined to `tools/ai/` (offline training/eval), matching existing
  precedent (`.venv-ai`, synthetic TinyBeaconNet training already uses this path).
  Production capture/detection/control/telemetry stays C++20.
- `v1_baseline` tag and the synthetic-trained Stage 1–4 AI work are not touched or
  replaced by this task.

## Next action

Awaiting G0 audit agent completion. Next: review `docs/LIVE_DATA_AUDIT.md`, then implement
the smallest G1 slice (one real camera → existing classical detector → a visible
DISCONNECTED-capable live page, no simulation fallback).
