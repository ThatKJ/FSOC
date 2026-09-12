# Live Data Audit (G0)

Date: 2026-09-12. Scope: trace every live-facing screen/API/provider on `main` to its
actual data source, verify the `feat/phone-camera-in-loop` branch's real content against
the claims in the pasted live-camera/real-data-AI prompt pack, and verify TinyBeaconNet's
actual I/O contract. Read-only — no code changed in this pass.

## 1. `main` today: every page is a finite-scenario replay, not a live feed

Every frontend route that looks "live" is actually driven by one API:

`GET /api/simulation/:scenario?source=engine|replay|auto&mode=classical|ai|hybrid&tracker=0|1`
(`frontend/app/api/simulation/[scenario]/route.ts`)

- `source=engine` (or `auto` when the engine is available) execs the **existing**
  `build/debug/fsoc_demo <scenario> --csv <tmp> --quiet [--duration N] [--mode] [--tracker]`
  binary to completion, reads the whole CSV it produced, and returns **all frames as one
  JSON array** (`frontend/lib/simulation/engine.ts`). This is a bounded, deterministic,
  finite run of a named scenario (`static|sinusoidal|loss|open|closed`) — there is no
  camera, no continuous frame stream, and no notion of "now."
- `source=replay` (or the `auto` fallback when the engine binary is missing) reads a
  **checked-in fixture JSON** under `frontend/lib/telemetry/fixtures/*.json`
  (`frontend/lib/simulation/fixtures.ts`) — a frozen array of pre-generated frames.
- The browser side (`frontend/lib/simulation/SimulationProvider.tsx` +
  `useScenarioFrames.ts`) fetches this array once per scenario selection and then plays it
  back on a local timer/scrubber. Nothing it shows was produced "this second"; it is either
  a fresh finite engine run or a frozen fixture, indistinguishable to the viewer.

This one provider feeds **every** route that consumes scenario frames: Dashboard/root
(`frontend/app/page.tsx`), Mission (`app/mission/page.tsx`), Tracking (`app/tracking/page.tsx`),
Telemetry (`app/telemetry/page.tsx`), Scenarios (`app/scenarios/page.tsx`), Benchmarks
(`app/benchmarks/page.tsx`), Validation (`app/validation/page.tsx`), World
(`app/world/page.tsx`), Architecture (`app/architecture/page.tsx`, static content only).

| Screen | Source today | Cosmetic or measurement | Live-session replacement |
|---|---|---|---|
| Dashboard / root | finite engine run or fixture via `SimulationProvider` | measurement (pixel/angular error, lock state) | subscribe to the one active `LiveSession` (camera-or-none); no scenario picker in this mode |
| Mission | same | measurement | primary live entry point (G1); real frame + matched detection overlay |
| Tracking | same | measurement | live frame + centroid/tracking-error overlay from the active session |
| Telemetry | same | measurement | active-session series only; reset on new session, not on new browser tab |
| Scenarios | same, `demoScenario` presets | measurement + preset config (cosmetic) | becomes "physical test instructions" (pack §C) — the deterministic scenario picker stays as a **regression/demo tool**, clearly separate from the live route |
| Benchmarks | same, `BenchmarkMetrics` (RMS/P95/max) computed over a finite run | measurement | stays as-is for the **synthetic regression suite**; a live/real-session evaluation view is new and separate (G3/G5), not a replacement of this page |
| Validation | Step-10 acceptance report (`generated/step10/VALIDATION_REPORT.md`) rendered/read | measurement, but explicitly a frozen historical acceptance record | stays as the historical `v1_baseline` record; real-session evaluation gets its own page, not a rewrite of this one |
| World | same provider, 3D-ish scene of world truth (position/FOV cone) | **this is the one page that cannot be honestly kept in the live path**: a single uncalibrated monocular camera has no world-frame pose/range. Per pack §C, remove from live navigation | out of live nav; may stay as a synthetic-scenario visualization only |

**Nothing here calls a "camera" today.** There is no existing simulation-vs-live toggle to
disable — the gap is that the live path (camera → detector → frontend) does not exist yet on
`main` at all. So "disable the fallback at the service boundary" (pack §C) is really: *build
the live boundary and never let it call `/api/simulation`*, not *patch an existing live route
that currently falls back to simulation*.

Config numbers that are legitimately cosmetic/engineering limits, not fabricated
measurements (do not flag or remove): `SCENARIOS`/`SIM_RATE_HZ` in
`frontend/lib/baseline/constants.ts` (scenario duration/expected-frame-count config), FOV/
actuator-rate/tilt-stop constants in the C++ camera config. These describe the **synthetic
regression suite**, which the pack explicitly allows to remain as isolated developer/demo
material.

## 2. What `feat/phone-camera-in-loop` actually contains (vs. the pasted pack's claims)

Verified via `git diff main...origin/feat/phone-camera-in-loop --stat` and `git show` (branch
not checked out, no working-tree changes made). The pack's claims are **substantially
accurate** and, if anything, understate how much is already built:

- `include/fsoc/frame_source.hpp` + `src/frame_source.cpp`: a clean `FrameSource`
  interface (`open/read/close/info`) with `FrameSourceKind{Synthetic, OpenCVCamera}`. Pure
  I/O boundary — "knows nothing about detection, perception mode, tracking, or control, and
  never touches world truth" (header comment, verified against implementation). This is
  exactly the seam CLAUDE.md's module-boundary rule requires and it already exists.
- `include/fsoc/opencv_camera_frame_source.hpp/.cpp`: `cv::VideoCapture`-backed
  `FrameSource`, by device index or URL. Confirmed real `cv::VideoCapture` usage, not a
  synthetic stand-in.
- `include/fsoc/live_camera_calibration.hpp/.cpp`: a config loader for calibration
  (intrinsics-adjacent) parameters, with its own tests (`tests/live_camera_calibration_tests.cpp`,
  162 lines).
- `include/fsoc/live_preprocessing.hpp/.cpp`: explicit resize/grayscale step from whatever
  the camera reports to the detector's `CV_8UC1` contract — **not** done silently inside the
  frame source (matches CLAUDE.md's "no hidden domain math" rule).
- `include/fsoc/live_tracking_session.hpp/.cpp`: `LiveTrackingSession` wires
  `FrameSource` output through the **existing, frozen** `BeaconDetector` /
  `AiBeaconDetector` / `resolve_perception()` (Safe Hybrid, ADR-018) / `TargetTracker`
  (ADR-019) / PID controller pipeline — it does not reimplement detection, perception
  fusion, or control. `LiveFrameResult` carries `frame_index`, `dt_s`, `source` info,
  perception fields, `tracked_state` (lock state, confidence, `is_prediction`), and a
  `command` — i.e. observed-vs-predicted is already a first-class distinct field
  (`is_prediction`), and the raw classical/AI candidate flags are preserved alongside the
  resolved detection (`classicalDetected`, `aiCandidateDetected` vs. `targetDetected`) —
  this already satisfies the pack's "don't let one boolean count predictions as detector
  success" requirement.
- `include/fsoc/virtual_actuator.hpp/.cpp`: `VirtualPanTiltActuator` — confirmed **honest
  bookkeeping only**. Header comment states explicitly it "does not move anything," must be
  labeled `ACTUATOR_TYPE = VIRTUAL`, and deliberately does *not* reuse `PanTiltCamera` to
  keep the simulation-vs-virtual-actuator boundary sharp. Matches the pack's claim exactly:
  the actuator is real bookkeeping math but zero physical motion.
- `apps/fsoc_live.cpp` (344 lines): the long-running live CLI. Confirmed:
  - `--source camera --camera-index N` or `--source camera-url --camera-url URL`.
  - `--mode classical|ai|hybrid`, `--tracker`, `--manual-assist` (prints human-readable
    PAN/TILT correction cues — this **is** the "manual alignment assist" deliverable tier,
    already implemented), `--no-control` (observe-only).
  - On read failure: retries up to 60 consecutive failures (~3s at 50ms backoff) before
    exiting cleanly with an explicit "camera appears disconnected" message — **no fabricated
    frames, no crash.**
  - Writes `generated/live/telemetry.json` and `generated/live/frame.jpg`, **overwritten
    every processed frame**, non-atomically (plain `ofstream`/`cv::imwrite`, no
    write-to-temp-then-rename). The code's own comments in the frontend routes acknowledge
    this directly ("fsoc_live overwrites the file every frame, non-atomically... report a
    partial write as a clean, retryable miss, never as fabricated telemetry").
  - Telemetry JSON already includes `frameIndex`, `cameraSource: "REAL_PHONE_CAMERA"`,
    `actuatorType: "VIRTUAL"`, raw + preprocessed dimensions, perception source/mode,
    lock state, `isPrediction`, command rates, and virtual pan/tilt + saturation flags —
    i.e. most of the pack's §E "versioned session/frame contract" fields already exist in
    an ad-hoc (non-versioned, no schema field, no session id) form.
- `apps/fsoc_camera_probe.cpp`, `apps/fsoc_camera_view.cpp`, `apps/fsoc_camera_calibrate.cpp`:
  device discovery/preview/calibration tools, confirmed present.
- `frontend/app/api/live-camera/route.ts` + `.../frame/route.ts`: **exactly** the 500ms
  independent-poll pattern the pack describes. Both routes are explicit and honest about
  their own limitation in code comments: "this route is a plain snapshot read... an honest
  polling read, not a live stream." `route.ts` reports `ageS` and a `stale` flag (>3s) —
  so basic staleness detection already exists, just not wired to a UI "disconnected" state
  yet, and not frame-ID-matched against the image.
- `frontend/app/mission/live/page.tsx` (255 lines): the live Mission Control page. Polls
  both routes independently on a timer; **does not currently verify the fetched image's
  frame identity matches the fetched telemetry's `frameIndex`** — this is the concrete gap
  the pack flags ("matching the image and measurements by frame identity is an
  implementation task"), confirmed real by reading the polling code, not assumed.
- `docs/PHONE_CAMERA_TEST_PLAN.md`, `docs/PHONE_CAMERA_GOLDEN_DEMO.md`,
  `docs/PHONE_CAMERA_METRICS.md`: already written on this branch, already candidly
  document the polling/latency/actuator-honesty limitations. **Reuse these, do not
  duplicate them** — the pack's request for these exact filenames is already satisfied by
  this branch's own docs once merged.
- `tools/beacon_display.html`: already exists — the exact "small bright dot on a phone
  screen" tool the pack's Setup A calls for.

**Net finding:** the branch is not a rough prototype; it is close to production-ready for
G1, built with the correct module boundaries, honest labeling, and mostly-graceful failure
handling already in place. It has never been merged into `main` (`main`'s log has no merge
commit for it, and diverged separately through the README/design-system commits). The one
concrete architectural gap for G1 is **frame/telemetry identity + atomicity**: two
independently-overwritten files, polled by two independent unsynchronized HTTP requests,
with no frame ID carried on the image side and no atomic pair swap. Everything else
(camera abstraction, honest actuator, failure handling, manual-assist cues, perception
reuse) is already correct and should be preserved, not rewritten.

**Recommendation for G1:** merge this branch into a new local integration branch off
`main` (not a rewrite from scratch), then fix identity/atomicity by (a) having
`fsoc_live` write frame-keyed immutable files (`frame_<N>.jpg`, `telemetry_<N>.json`) via
write-to-temp-then-`rename()` (atomic on POSIX) plus a small `manifest.json` naming the
latest complete pair's frame index, and (b) having the frontend fetch the manifest first,
then fetch exactly that frame's image+telemetry by ID, rejecting/retrying on any mismatch.
This satisfies the pack's §E requirements without introducing a WebSocket server, which the
pack explicitly allows ("another transport is acceptable if it proves the same identity,
freshness, and backpressure guarantees").

## 3. TinyBeaconNet actual contract (verified against `models/MODEL_CARD.md` and
   `tools/ai/common.py`, not assumed from the pasted pack's prose)

- Native/reference frame: `CV_8UC1`, **640×480**.
- Preprocess: `cv2.resize(..., (320, 240), INTER_AREA)` → float32/255 → NCHW
  **`[1, 1, 240, 320]`** — confirms the pack's claim exactly (320×240 net input, 640×480
  reference image).
- Outputs: `presence_logit [N,1]`, `heatmap_logit [N,1,60,80]`, sigmoid applied **outside**
  the graph on both, decode via integer argmax + 5×5 soft-argmax, mapped back to original
  coordinates via `x_orig = (x_hm + 0.5)·8 − 0.5` — confirms 80×60 heatmap claim exactly.
- Training data: **synthetic only**, `fsoc_ai_datagen` seed 26169, 8400 total frames
  (6000/1200/1200 train/val/test), verified integrity (unique hashes, no leakage). Model
  card's own header states plainly: "trained + validated on synthetic data... NOT
  integrated into the C++ closed loop [was true at Stage 2; Stage 3 has since integrated
  it, still synthetic-trained]."
- **Confirmed gap for G3**: `tools/ai/dataset.py` currently reads only the synthetic
  `fsoc_ai_datagen` JSONL/manifest format. A real-capture dataset reader, grouped
  train/val/test split (by recording session, not frame), and a reviewed-label manifest
  format do not exist yet and must be added additively (new dataset reader class/mode, not
  a rewrite of the synthetic path — Stage 1–4 synthetic training/eval must keep working
  unchanged for regression comparison).

## 4. Module boundaries a live camera path must respect (already established, reuse as-is)

- `FrameSource` (`include/fsoc/frame_source.hpp`) — the correct seam for camera vs.
  synthetic. Do not add a second competing abstraction.
- `fsoc::BeaconDetection` / classical detector — unchanged, frozen contract.
- `fsoc::AiBeaconDetector`, `PerceptionMode`, `resolve_perception()` (Safe Hybrid,
  ADR-018) — unchanged; a live path selects a mode, it does not alter fusion policy.
- `fsoc::TargetTracker` (alpha-beta, ADR-019), `LockState{Searching,Acquiring,Tracking,
  Coasting,Lost}`, `is_safe_to_steer()` — unchanged; already distinguishes measured vs.
  predicted vs. lost, which the pack requires.
- `fsoc::VirtualPanTiltActuator` — unchanged; stays the actuator until real hardware (G6)
  exists. Its telemetry must keep saying `VIRTUAL`/manual-assist, never a claim of physical
  motion, consistent with the user's confirmed "no hardware yet" state.
- `fsoc::LiveTrackingSession` — the orchestration seam; the only new work is the
  frame/telemetry identity+atomicity fix described above, plus (later) recording hooks
  (G2) added as an additional observer, not inline in this class.
- Telemetry: the existing 34/42-column CSV schema is for the **finite scenario** path
  (`fsoc_demo`/`SimulationRunner`). The live path's JSON schema is separate (per-frame,
  overwritten) and already close to what's needed — G1 work is to version it and add
  `sessionId`/`schemaVersion`/calibration/model-hash fields per pack §E, not to unify it
  with the CSV schema.

## 5. Bottom line / next action

G0 is complete. The single biggest concrete gap between `main` and a real continuous
camera→detector→frontend path is: **the phone-camera branch was never merged, and even once
merged, the frontend/backend frame–telemetry pairing is not identity-safe** (two files,
two independent polls, no shared frame ID enforcement, no atomic pair swap). Everything
else needed for a defensible G1 (camera abstraction, detector reuse, honest actuator,
failure handling, manual-assist) already exists in working, tested C++ form on
`feat/phone-camera-in-loop` and should be integrated, not rebuilt.

Next action (G1): create a local integration branch off `main`, merge in the relevant
`feat/phone-camera-in-loop` commits/files, resolve conflicts against `main`'s
README/design-system/smoke-test changes, implement the frame-keyed atomic write/read fix,
build, and run the existing branch's own C++ tests (`frame_source_tests`,
`live_camera_calibration_tests`, `live_preprocessing_tests`, `live_tracking_session_tests`,
`virtual_actuator_tests`) plus a new test for the atomic pairing contract.
