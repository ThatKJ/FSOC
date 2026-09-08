# Phone Camera-in-the-Loop — Test Plan

Split per this milestone's own instructions: not everything can be automated because the
phone/camera is external hardware. See `docs/PHONE_CAMERA_METRICS.md` for the full
architecture and the honest reason live-hardware capture was not run from this coding
session.

## Automated (CTest, run in this session — all currently PASS)

| Test | What it covers |
|---|---|
| `fsoc_virtual_actuator_tests` | Config validation, rate saturation, integration, tilt travel limit, reset, invalid-argument rejection — OpenCV-free |
| `fsoc_live_camera_calibration_tests` | Save/load roundtrip, validation rejection, `CameraConfig` conversion, both FOV-estimation methods — OpenCV-free |
| `fsoc_frame_source_tests` | `FrameSourceKind` labeling, config's exactly-one-of-index-or-url enforcement, an out-of-range camera index failing cleanly (no real device touched) |
| `fsoc_live_preprocessing_tests` | Grayscale passthrough, BGR/BGRA-to-grayscale conversion, custom target size, empty-frame / invalid-size / unsupported-channel-count rejection |
| `fsoc_live_tracking_session_tests` | Pixel-error quadrant signs (fabricated frames, no camera), no-target leaves no ground truth and zero command, `control_enabled=false` never commands, a real command's sign matches the tracking error, tracker wiring coasts through a brief gap and reacquires, `reset()` clears controller/actuator state, invalid-argument rejection, AI/Hybrid mode requires an AI detector config |

Run: `cmake --build --preset debug && ctest --preset debug --output-on-failure` (these run
alongside every existing Step 1–11 / Stage 3/4 test — all must stay green; see "Baseline
preservation" in the final report for this milestone).

## Manual validation (run these yourself — fill in PASS/FAIL)

Each of these requires a real phone/webcam and, on macOS, an interactive terminal session
that can answer the camera-permission prompt — this is why they were not run automatically.
See `docs/PHONE_CAMERA_GOLDEN_DEMO.md` for the exact commands.

| # | Test | Acceptance criteria | Result |
|---|---|---|---|
| M1 | Camera discovery | `fsoc_camera_probe` lists at least one AVAILABLE index with a sane resolution | ☐ PASS ☐ FAIL |
| M2 | Camera opens and stays stable | `fsoc_camera_view --seconds 60` runs the full 60s with no crash, no repeated reconnection loop, no uncontrolled memory growth (watch `top`/Activity Monitor) | ☐ PASS ☐ FAIL |
| M3 | Timestamps increase correctly | `fsoc_camera_view` stdout shows strictly increasing `t=` values, no backward-time warning | ☐ PASS ☐ FAIL |
| M4 | Calibration produces a sane FOV | `fsoc_camera_calibrate --manual ...` (or `--from-object`) writes a file `fsoc_camera_calibrate --check` reads back correctly | ☐ PASS ☐ FAIL |
| M5 | Real target detected (Classical) | `fsoc_live --mode classical` with `tools/beacon_display.html` on a monitor: `targetDetected=true`, plausible `detectedXPx/YPx` | ☐ PASS ☐ FAIL |
| M6 | Real Hybrid perception | `fsoc_live --mode hybrid --ai-model models/tiny_beacon_net.onnx`: `perceptionMode=HYBRID`, a real `aiPresenceProbability` appears | ☐ PASS ☐ FAIL |
| M7 | Angular error sign convention | Move the beacon right of centre -> `panErrorDeg > 0`; left -> `< 0`; above -> `tiltErrorDeg > 0`; below -> `< 0` (matches `fsoc/tracking_error.hpp`) | ☐ PASS ☐ FAIL |
| M8 | Manual Correction Assist cue direction | With `--manual-assist`, moving the phone in the printed direction visibly reduces the printed error magnitude on the next frame | ☐ PASS ☐ FAIL |
| M9 | Phone motion test | Physically pan the phone left/right across the beacon; `commandPanRateDegS` sign tracks the visible offset direction | ☐ PASS ☐ FAIL |
| M10 | Occlusion / coasting | `--tracker`: briefly cover the beacon; `lockState` goes `TRACKING -> COASTING`, `isPrediction=true`, then `-> TRACKING` on uncovering | ☐ PASS ☐ FAIL |
| M11 | Long occlusion / reacquisition | Cover the beacon beyond `max_coast_frames`; `lockState` reaches `LOST` then `SEARCHING`, then re-acquires (`ACQUIRING -> TRACKING`) once revealed | ☐ PASS ☐ FAIL |
| M12 | Clutter / false-lock investigation | Introduce a second bright source (another screen/lamp) alongside the beacon; record what actually happens (`perceptionSource`, any lock switch) — do NOT expect this to be solved; this is exploratory, per docs/MVP_ABLATION.md's own disclosed limitation | ☐ PASS ☐ FAIL / OBSERVED: _______ |
| M13 | Camera disconnect handled safely | Unplug/close the camera mid-run; `fsoc_live` prints the "too many consecutive frame read failures" message and exits cleanly (no crash) | ☐ PASS ☐ FAIL |
| M14 | Mission Control shows REAL/VIRTUAL labels | With `fsoc_live` running, `/mission/live` shows `CAMERA SOURCE: REAL_PHONE_CAMERA` and `ACTUATOR: VIRTUAL` and the live frame updates | ☐ PASS ☐ FAIL |
| M15 | Mission Control handles no session honestly | With `fsoc_live` NOT running, `/mission/live` shows "No live session" — never fabricated telemetry | ☐ PASS ☐ FAIL (already verified in this session — see final report) |

## What was verified automatically vs. left for you

Verified in this session: M15 (the "no live session" honest-failure path), plus every
automated test above and a full manual page-render check with fabricated (clearly
scratch, gitignored, deleted afterward) telemetry — see the final report's "Mission Control
integration" section.

Left for you (M1–M14): genuine camera hardware access, per `docs/PHONE_CAMERA_METRICS.md`'s
"What could not be measured" note.
