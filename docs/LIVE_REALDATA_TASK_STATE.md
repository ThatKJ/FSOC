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
| G1 | Physical live baseline (real camera → classical tracking → matched frame/telemetry) | AUTOMATED PASS — see below; PHYSICAL PASS pending operator run |
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

**Branch:** `feat/live-camera-integration` (local only — not pushed, `main` untouched).

- [x] Merged `origin/feat/phone-camera-in-loop` into a new local integration branch off
      `main` (commit `ba253a7`). Resolved README.md conflicts by combining main's
      rewritten landing-page prose with the branch's Mobile Phone Camera-in-the-Loop /
      Hardware boundary / Working modes sections; corrected now-stale claims (LICENSE
      file now exists; phone-camera work is merged, not a separate branch).
- [x] Fixed the frame↔telemetry identity/atomicity gap identified in the audit: added
      `fsoc::LiveFramePublisher` (`include/fsoc/live_frame_publisher.hpp`,
      `src/live_frame_publisher.cpp`) — writes `frame_<N>.jpg` / `telemetry_<N>.json` via
      write-to-temp-then-atomic-rename, then flips `manifest.json` (same pattern) LAST,
      so any reader that observes a frame index in the manifest is guaranteed both files
      for that index already exist, complete. Bounded retention (default 3 pairs) so
      `generated/live/` cannot grow unbounded during a long session.
  - 3 new unit tests (`tests/live_frame_publisher_tests.cpp`, `fsoc_live_frame_publisher_tests`
    in CTest): config validation, single-publish completeness + no leftover `.tmp` files,
    and a 25-frame run proving the manifest-named pair always exists on disk and older
    pairs are pruned.
- [x] Rewired `apps/fsoc_live.cpp` to use the publisher instead of overwriting
      `telemetry.json`/`frame.jpg` in place; added a per-run `sessionId`
      (wall-clock-ms token — a reconnect always gets a new one) and `schemaVersion`/
      `sessionId` fields in the telemetry JSON.
- [x] Rewired `frontend/app/api/live-camera/route.ts` to read `manifest.json` then the
      exact `telemetryFile` it names, returning `frameIndex` to the client.
      Rewired `frontend/app/api/live-camera/frame/route.ts` to require `?frame=<index>`
      and serve exactly that file (400 on missing/invalid index, 404 if already pruned —
      never a mismatched fallback image).
      Rewired `frontend/app/mission/live/page.tsx` to fetch the image by the exact
      `frameIndex` the telemetry response named (object-URL fetch, not a cache-busted
      `<img src>`), and to drop the displayed frame when `sessionId` changes
      (reconnect must not inherit the previous session's last image).
- [x] Build: `cmake --preset debug -DFSOC_ENABLE_OPENCV=ON && cmake --build --preset debug`
      — clean, no errors/warnings. `ctest --preset debug --output-on-failure` — **23/23
      PASS** (22 pre-existing + `fsoc_live_frame_publisher_tests`).
- [x] Frontend: `npm ci && npm run typecheck && npm run lint && npm run build` — all
      clean. `npx playwright test` — **51/51 PASS** (after clearing a stale `next dev`
      process from earlier in this session that was holding port 4317 against an old
      Next.js version and corrupting `.next/`; not a regression from this work).
- [x] End-to-end wiring smoke-tested with a hand-built fixture (mimicking
      `LiveFramePublisher`'s exact output) against a real running server: confirmed
      `/api/live-camera` returns the correct `frameIndex`/`ageS`/`stale`, and
      `/api/live-camera/frame?frame=<N>` serves the exact matching bytes, 404s on an
      unpublished/pruned index, 400s on a missing index — without opening a camera.
- [ ] **PHYSICAL PASS — requires the operator.** Opening a real camera device needs an
      interactive session to answer the OS permission prompt (macOS TCC) — this cannot
      be done from an unattended shell. See "Next action" below.

Known limitation carried forward (not blocking, documented not hidden): fsoc_live only
publishes a new manifest/frame/telemetry entry when a frame is successfully read and
processed. A stalled-but-still-reading camera (never crossing the 60-consecutive-failure
disconnect threshold) is only visible to the frontend indirectly, via the existing
`ageS`/`stale` check on the last published frame — there is no independent
"camera status" heartbeat written on read failure. Acceptable for G1; worth revisiting
if real testing shows this staleness signal is too slow to be trusted.

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

## Next action — operator required (unblocks PHYSICAL PASS for G1)

The live-camera software path is implemented, built, and automatically tested. `configs/`
is currently empty — no calibration file is committed — and opening a camera device needs
an interactive session to answer macOS's camera-permission prompt, so this step is yours.

The existing `docs/PHONE_CAMERA_GOLDEN_DEMO.md` (step-by-step walkthrough) and
`docs/PHONE_CAMERA_TEST_PLAN.md` (acceptance matrix M1–M15) already cover this — reuse
them rather than a new script. They were written against `--camera-index` on a phone
feeding in over a stream; for your confirmed setup (laptop webcam + phone-screen-dot
beacon) the only change is the calibration numbers (a laptop webcam's FOV is typically
narrower than a wide-angle phone lens) and that "point the camera at the phone" replaces
"point the phone at a monitor."

1. `./build/debug/fsoc_camera_probe` — find your webcam's index and resolution, and
   answer the OS camera-permission prompt if one appears.
2. Calibrate (`configs/` is empty, so this file doesn't exist yet):
   `./build/debug/fsoc_camera_calibrate --manual --width <W> --height <H> --hfov-deg <D> --vfov-deg <D2> --out configs/webcam.cfg`
   using the resolution from step 1. If you don't know your webcam's FOV, use the
   `--from-object` form instead (see `docs/PHONE_CAMERA_METRICS.md` "Camera calibration"
   for the method) — measure a known-width object at a known distance.
3. Display `tools/beacon_display.html` on your phone (small bright dot, dark background)
   where the webcam can see it.
4. `./build/debug/fsoc_live --source camera --camera-index <N> --calibration configs/webcam.cfg --manual-assist`
   — expect continuous `frame ... lock=... detected=...` lines, and
   `generated/live/manifest.json` + `frame_<N>.jpg` + `telemetry_<N>.json` appearing.
5. `cd frontend && npm run dev`, open `http://localhost:4317/mission/live` — expect LIVE
   (not STALE), the real feed with the beacon, `CAMERA SOURCE: REAL_PHONE_CAMERA` /
   `ACTUATOR: VIRTUAL`, and the pointing-error panel updating as you move the phone.
6. Work through `docs/PHONE_CAMERA_TEST_PLAN.md` M4–M15 (detection, sign convention,
   occlusion/coasting, disconnect, Mission Control honesty) — M13/M15 are exactly the
   disconnect/no-fabrication checks this task cares about; M14 now also implies the new
   `frameIndex`-matched image/telemetry pairing should hold (no visibly mismatched frame).
7. Additionally: stop `fsoc_live` (Ctrl+C), confirm the page goes STALE within ~3s, then
   restart it, and confirm the page picks up the new `sessionId` and drops the old
   session's last displayed frame rather than showing it under a new "LIVE" label.

Report back what you observe (or paste any error) and I'll mark G1 PHYSICAL PASS and
move to G2 (recording + annotation tooling) — or fix whatever step 1-7 turns up first.
