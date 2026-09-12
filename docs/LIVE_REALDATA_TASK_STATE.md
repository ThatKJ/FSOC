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
| G2 | Measurement and data collection (recording, annotation, splits) | AUTOMATED PASS (software) — real reviewed recordings pending operator |
| G3 | Real-data AI (train/eval TinyBeaconNet on reviewed real captures) | BLOCKED — no reviewed real recordings exist yet |
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
- [x] **Uncalibrated (pixel-only) mode added** so a real preview never has to wait on
      calibration. `fsoc_live --uncalibrated` (mutually exclusive with `--calibration
      PATH`): builds the sensing camera from the header's documented placeholder FOV,
      but `to_json()` always nulls `panErrorDeg`/`tiltErrorDeg`/`totalErrorDeg` when
      uncalibrated (regardless of what the placeholder-FOV math internally computes),
      adds an explicit `calibrationStatus: "UNCALIBRATED"` telemetry field, and
      force-disables control/actuation (a fabricated FOV must never drive a command).
      `--manual-assist` and the terminal `error=` line switch to real pixel offsets
      instead of fabricated degrees in this mode. `/mission/live` shows a
      **CALIBRATION** row (`CALIBRATED` vs `UNCALIBRATED (pixel-only)`) and a
      **SESSION ID** row (both new).
- [x] **Real per-frame timing surfaced, not just implied.** `/mission/live` now shows a
      TIMING panel: PROCESSED FPS (`1/dtS`, the C++ side's own measured wall-clock
      interval — explicitly labeled as real per-frame timing, not an exposure
      timestamp) and DISPLAYED FPS (measured client-side from actual image updates).
      Poll interval tightened 500ms → 200ms (still ~5-6x below a typical webcam's
      native 15-30fps — this gap is shown by the two FPS numbers, not hidden).
- [x] **Frontend risk review, focused (not a re-check of already-verified work):** added
      `frontend/tests/e2e/live-camera.spec.ts` (6 tests, route-mocked, no camera needed)
      covering exactly the previously-untested risks — reconnect (new `sessionId`) drops
      the prior session's displayed frame image, a pruned/missing frame (404) doesn't
      crash or show a mismatched image, the honest no-session and stale states render
      distinctly, and the recording panel reflects telemetry (not local optimistic
      state). `/mission/live` was only covered by generic responsive-layout checks
      before this.
- [ ] **PHYSICAL PASS — requires the operator.** Opening a real camera device needs an
      interactive session to answer the OS permission prompt (macOS TCC) — this cannot
      be done from an unattended shell. See "Camera setup checklist" and "Physical G1
      review script" below.

Known limitation carried forward (not blocking, documented not hidden): fsoc_live only
publishes a new manifest/frame/telemetry entry when a frame is successfully read and
processed. A stalled-but-still-reading camera (never crossing the 60-consecutive-failure
disconnect threshold) is only visible to the frontend indirectly, via the existing
`ageS`/`stale` check on the last published frame — there is no independent
"camera status" heartbeat written on read failure. Acceptable for G1; worth revisiting
if real testing shows this staleness signal is too slow to be trusted.

## G2 — Measurement and data collection

**Software: AUTOMATED PASS.** No real reviewed recordings exist yet — that half of G2
is BLOCKED on the operator (see "Recording assignment" below), and is tracked
separately from the software.

- [x] **Recording controls.** `/mission/live` has Start/Stop Recording buttons plus
      three preset event-mark buttons (Covered/Visible/Scene Change), wired through
      `POST /api/live-camera/record` → `generated/live/command.txt` (key=value,
      temp-then-rename) → polled once per camera-frame iteration by `fsoc_live`. A 200
      response only means the command file was written; the UI's RECORDING status
      always reflects the next polled telemetry (`recordingActive`/`recordingId`/
      `recordedFrameCount`/`recordingErrorCount`), never local optimistic state — same
      honesty rule already applied to the camera feed itself. Documented limitation:
      `command.txt` is a single slot, not a queue — two commands faster than one camera
      frame interval apart will have the earlier one superseded (unrealistic for a
      human clicking a button; noted in `apps/fsoc_live.cpp`'s file header).
- [x] **`fsoc::RealSessionRecorder`** (`include/fsoc/real_session_recorder.hpp`,
      `src/real_session_recorder.cpp`) — a *second*, separate sink from
      `LiveFramePublisher`'s pruned preview buffer. Writes, under
      `generated/real_sessions/<recordingId>/`:
      `manifest.json` (session/recording id, calibration status+id, perception
      mode+model path, `softwareCommit` — captured at CMake configure time via `git
      rev-parse HEAD`, not re-queried at runtime — full `cliArgs`, raw+preprocessed
      dims, source backend/description, running frame/error/event counters,
      rewritten atomically after every frame so a crash mid-recording still leaves a
      valid partial manifest), `frames/frame_<N>.jpg` (RAW, no overlay — every frame
      accepted is kept, **never pruned**, unlike the live preview buffer),
      `telemetry.jsonl` (append-only, one line per frame, same content as the live
      telemetry), `events.jsonl` (human markers, tagged with the last recorded
      frame). Writes are synchronous per frame (no thread, no in-memory queue) — a
      disk error increments a counted `error_count()` and is swallowed, never stalls
      the tracking loop.
  - 4 new unit tests (`tests/real_session_recorder_tests.cpp`,
    `fsoc_real_session_recorder_tests`): config validation, a 40-frame recording
    surviving in full (nothing pruned) with a correct manifest, event markers tagged
    to the right frame, and a simulated disk-write failure counted, not thrown.
- [x] **Annotation tool** at `/mission/annotate` (reachable from `/mission/live` →
      "Review Recordings"). Lists local recordings (`GET /api/real-sessions`), loads
      one's manifest+telemetry (`GET /api/real-sessions/:id`), serves its raw frames
      (`GET /api/real-sessions/:id/frame/:index`), and reads/writes reviewed labels
      (`GET`/`POST /api/real-sessions/:id/labels` → `labels.json`, keyed by frame
      index so re-labeling a frame updates it rather than appending a duplicate).
      Supports: frame scrubbing, presence labeling (present / partial occlusion /
      full occlusion / absent / ambiguous), click-to-set beacon center (in **raw**
      pixel space — `labelCoordinateSpace: "raw"` recorded explicitly), a detector
      **suggestion** marker from the recording's own telemetry (visually distinct,
      never auto-saved — only becomes a label if a human accepts/adjusts it and hits
      Save), and a reviewed/total progress count. Server-side validation rejects a
      self-contradictory label (e.g. `present` with no center, `absent` with a
      center) before it can be saved. Raw frame files on disk are never touched —
      all overlays are browser-side only.
  - 4 new e2e tests (`frontend/tests/e2e/annotate.spec.ts`, route-mocked): empty
    state, save-without-center rejected, click-then-save persists a reviewed label,
    and navigating frames does not carry over the previous frame's unsaved draft.
- [x] **Real-data dataset loader** (`tools/ai/real_dataset.py`) — additive to
      `dataset.py`'s synthetic `BeaconDataset`; the synthetic path is untouched.
      `RealBeaconDataset` reads reviewed recordings and returns the *same*
      `(input[1,240,320], heatmap[60,80], present, label_xy[2], difficulty)` tuple
      shape, so real and synthetic samples can be combined with
      `torch.utils.data.ConcatDataset` later without special-casing either.
  - Validates: manifest/labels.json/frames exist; `labelCoordinateSpace == "raw"`;
    a `present`/`partial_occlusion` label has a center *inside that recording's own*
    raw frame bounds; an `absent`/`full_occlusion` label has no center; a labeled
    frame's image file actually exists. Raises `RecordingValidationError` on any of
    these rather than silently skipping a broken recording.
  - Exclusion policy (explicit): `ambiguous` and any frame missing from
    `labels.json` (never reviewed) are excluded from every returned sample —
    never forced into a positive or negative target.
  - Coordinate transform: each raw frame is resized to the frozen `common.ORIG_W ×
    ORIG_H` (640×480) the same way `LivePreprocessConfig` already resizes a real
    frame before detection, and a label's raw-pixel center is scaled by the *same*
    per-recording ratio (`_raw_to_orig()` — the one function to touch if crop/mirror
    is ever added to the real-camera preprocessing pipeline).
- [x] **Group-based split** (`tools/ai/real_dataset_split.py`) — splits by
      **recording** (capture group), never by frame; `assign_splits()` raises
      `ValueError` rather than silently leaving a split empty when there are too few
      groups (e.g. splitting 1-2 recordings three ways). Deterministic (seeded),
      persists one JSON manifest (`save_split_manifest`/`load_split_manifest`) so the
      same partition is reused across runs instead of re-derived. CLI:
      `python tools/ai/real_dataset_split.py --real-sessions-root generated/real_sessions --out <path>`.
- [x] **Plumbing tests, clearly-labeled fixtures only**
      (`tools/ai/real_dataset_tests.py`, 13 checks, run manually — same convention as
      `selfcheck.py`, not wired into CTest since it needs `.venv-ai`): missing
      manifest/labels/frame-file all raise; wrong `labelCoordinateSpace` raises;
      out-of-bounds / missing / contradictory centers raise; `ambiguous` and
      never-reviewed frames are excluded; a known raw-space label decodes back out
      (via the frozen heatmap encode/decode round trip) close to its expected
      resized coordinate; splits have zero group leakage and are deterministic per
      seed; too-few-groups raises; split manifest round-trips exactly. Explicitly
      documented as fixtures, not real training data.
      Run: `.venv-ai/bin/python3 tools/ai/real_dataset_tests.py` (from
      `tools/ai/`) — **PASS: all 13 checks**.
- [ ] **Real reviewed recordings.** None exist yet — this is the actual G2/G3
      dependency on you. See "Recording assignment" below.

## G3 — Real-data AI

**BLOCKED.** Not started, and must not be claimed complete or attempted with
placeholder data: no reviewed real recordings exist yet, so there is nothing to
train or evaluate on. Unlocked once the first reviewed batch from "Recording
assignment" below exists — the dataset loader and split tooling are already built
and tested against fixtures, ready for that data the moment it exists.

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
- Recordings/labels/split manifests live under `generated/` (already fully
  git-ignored) — device-specific, local by default. Nothing under `generated/` is
  committed; `configs/` stays for reusable *templates* (e.g. an example calibration
  file), never a guessed real measurement presented as this operator's actual device.

## Camera setup checklist — laptop webcam + phone-screen-dot beacon

Verified in this session (commands/flags actually exist and were exercised, short of
opening a real camera). Run from the repo root unless noted.

1. **Build the camera tools** (if not already built):
   `cmake --preset debug -DFSOC_ENABLE_OPENCV=ON && cmake --build --preset debug`
   Confirms via `-- FSOC: OpenCV videoio present - phone-camera-in-the-loop targets
   enabled` in the configure output.
2. **Probe for your webcam's index**: `./build/debug/fsoc_camera_probe` (probes
   indices 0..4 by default; add `--max-index N` for more). Prints a table of
   resolution/FPS/backend/status per index — note the first `AVAILABLE` one.
3. **Raw preview — no calibration needed for this step**:
   `./build/debug/fsoc_camera_view --camera-index <N> --seconds 15 --out-dir generated/camera_view_test --crosshair`
   (a separate `--out-dir` from `generated/live` avoids any confusion with
   `fsoc_live`'s own output). Writes periodic JPEG snapshots to that directory —
   open one to confirm you're actually seeing your webcam, before calibration is
   ever a blocker.
4. **The macOS camera-permission prompt** appears the first time step 2 or 3 opens
   the device from your terminal app. If you don't see a prompt and the probe
   reports every index `UNAVAILABLE`, permission was likely denied previously —
   check System Settings → Privacy & Security → Camera and enable it for the
   terminal app you're running this from, then re-run step 2.
5. **Calibration — pixel-only fast path (recommended first)**: skip calibration
   entirely with `fsoc_live --uncalibrated` (see step 6) — no file, no measurement,
   real pixel-offset tracking immediately, degrees/control explicitly unavailable.
   For real angular numbers later: `./build/debug/fsoc_camera_calibrate --manual
   --width <W> --height <H> --hfov-deg <D> --vfov-deg <D2> --out configs/webcam.cfg`
   using the resolution from step 2 and your webcam's actual spec'd or measured
   field of view (never copy the simulator's 20°/15°) — or the `--from-object` form
   (see `docs/PHONE_CAMERA_METRICS.md` "Camera calibration") if you measure a
   known-width object at a known distance instead. `configs/` is currently empty;
   treat anything you save there as your own local device file, not something to commit
   as if it were a universal value.
6. **Start fsoc_live**: display `tools/beacon_display.html` on your phone (small
   bright dot, dark background) where the webcam can see it, then:
   `./build/debug/fsoc_live --source camera --camera-index <N> --uncalibrated --manual-assist`
   (swap `--uncalibrated` for `--calibration configs/webcam.cfg` once you have one).
   Expect continuous `frame ... lock=... detected=...` lines and
   `generated/live/manifest.json` + `frame_<N>.jpg` + `telemetry_<N>.json` appearing.
7. **Open Mission Control**: `cd frontend && npm run dev`, then
   `http://localhost:4317/mission/live`. Expect LIVE (not STALE), the real feed with
   the beacon, `CALIBRATION: UNCALIBRATED (pixel-only)` (or `CALIBRATED` if you used
   step 5's file), a SESSION ID, and the pointing-error/TIMING panels updating live.

## Physical G1 review script — run once camera setup above works

Return, for each step: what you saw (screenshot if easy), and any terminal
output/error. This is what turns G1 from AUTOMATED PASS to PHYSICAL PASS.

1. Show an unpredictable hand movement or a handwritten number to the camera —
   confirms the feed is current, not a loop/replay (frame index and TIMING numbers
   should keep advancing).
2. Show the phone-dot beacon; confirm the pointing-error panel goes non-zero and
   `targetDetected`/lock state respond.
3. Move the phone left / right / up / down; confirm the pixel-error/pan-tilt sign
   matches the direction (see `docs/PHONE_CAMERA_TEST_PLAN.md` M7 for the exact
   convention).
4. Cover the beacon while leaving the camera running: confirm lock state changes
   (or `targetDetected: false`) while the page stays LIVE (camera still connected,
   just no target) — never a fabricated detection.
5. Uncover it: confirm reacquisition (lock state returns, `isPrediction: false` on
   the first fresh detection).
6. Stop `fsoc_live` (Ctrl+C): confirm the page goes STALE within ~3s, then shows the
   disconnected/no-session state — no replay, no lingering green "LIVE".
7. Restart `fsoc_live`: confirm the page shows a **new SESSION ID** and does not
   keep showing the previous session's last frame under a fresh "LIVE" label.
8. Note the PROCESSED FPS / DISPLAYED FPS panel values you actually observed — this
   is the real, measured number, not an assumption.

Also work through `docs/PHONE_CAMERA_TEST_PLAN.md` M4–M15 if you want the fuller
matrix (M13/M15 are the same disconnect/no-fabrication checks as steps 6-7 above).

## Recording assignment — the first plumbing clip (G2 → unlocks G3)

One ~60-second recording, using the Start/Stop Recording controls on
`/mission/live` (camera + beacon set up per the checklist above):

1. **0:00–0:15 — stationary beacon.** Don't move the phone or camera.
2. **0:15–0:30 — slow horizontal then vertical movement.** Pan the phone/beacon
   left-right, then up-down, slowly enough to stay trackable.
3. **0:30–0:45 — cover and uncover.** Use the "Mark: Covered" / "Mark: Visible"
   buttons at the moments you actually cover/uncover it.
4. **0:45–1:00 — beacon absent.** Point the camera away from the beacon; include
   another bright object (a lamp, a phone flashlight) in frame if you have one
   handy — use "Mark: Scene Change" when you do.

Click **Start Recording** before step 1, work through 1-4, then **Stop Recording**.
The recording lands at `generated/real_sessions/<recordingId>/` (the id is shown in
the RECORDING panel and in the terminal). Then:

5. Open `http://localhost:4317/mission/annotate`, select that recording, and label
   a handful of frames across all four segments (present / absent / partial or
   full occlusion as appropriate) — you don't need to label every frame for this
   first pass, just enough to prove the workflow: click the beacon center, choose a
   presence value, hit Save Label, confirm "already reviewed" appears, move to the
   next frame.

Report back: the `recordingId`, roughly how many frames you labeled, and anything
that felt broken or confusing in either page. **This first clip only verifies the
recording/annotation workflow — it is not a sufficient training dataset and not an
independent evaluation set.** Once it works, I'll give you the next batch: several
more short sessions (varied distance/brightness/edges-of-frame per
`docs/LIVE_REALDATA_TASK_STATE.md`'s own future entry once written), with specific
sessions held out for validation and a separate final test group — collected before
any training run, per the split tooling above.
