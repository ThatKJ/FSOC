# Phone Camera-in-the-Loop — Golden Demo

The strongest possible demo using zero additional hardware: your Mac's camera (or a phone
used as a webcam/continuity camera), a monitor as the target, no servos, no Arduino. Every
step runs a real command against real hardware — nothing here is scripted or pre-recorded.
See `docs/PHONE_CAMERA_METRICS.md` for the full architecture and claim boundary, and
`docs/PHONE_CAMERA_TEST_PLAN.md` for the manual validation checklist this demo exercises.

**Run this yourself, interactively.** Opening a camera triggers an OS permission prompt
(macOS: System Settings → Privacy & Security → Camera) that only a real interactive terminal
session can answer — this is why the commands below were not run automatically when this
milestone was built.

## Prerequisites (once)

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
```
Expect `100% tests passed out of 22` (17 pre-existing + 5 for this milestone).

## Setup

**1. Open the beacon display on a second window/monitor.**
```bash
open tools/beacon_display.html   # macOS; or just double-click it
```
Press `F` for fullscreen. Leave motion on "Static" for now.

**2. Find your camera index.**
```bash
./build/debug/fsoc_camera_probe
```
Note the `AVAILABLE` index with the resolution you expect (built-in cameras are usually 0).

**3. Prove the camera feed is real and stable before any tracking logic runs.**
```bash
./build/debug/fsoc_camera_view --camera-index 0 --seconds 15 --crosshair
```
Point the camera at the beacon display. Confirm: frames arrive, `t=` timestamps increase,
`generated/live/` (or `--out-dir`) fills with JPEG snapshots you can open and look at.

**4. Calibrate.** Either declare your camera's known spec-sheet FOV:
```bash
./build/debug/fsoc_camera_calibrate --manual --width 1920 --height 1080 \
    --hfov-deg 69 --vfov-deg 42 --out configs/phone_camera.cfg
```
...or estimate it from a known object (see `docs/PHONE_CAMERA_METRICS.md` "Camera
calibration" for the method):
```bash
./build/debug/fsoc_camera_calibrate --from-object --object-width-m 0.30 --distance-m 1.0 \
    --object-pixel-width-px 400 --image-width-px 1920 --image-height-px 1080 \
    --out configs/phone_camera.cfg
```

## The demo

**5. Real Classical perception on a real frame.**
```bash
./build/debug/fsoc_live --source camera --camera-index 0 \
    --calibration configs/phone_camera.cfg --mode classical
```
Point at the beacon. Watch `detected=yes` and a real, changing `error=` in the terminal.
This is the exact same `BeaconDetector` the simulation uses (`docs/PHONE_CAMERA_METRICS.md`
"Architecture").

**6. Launch Mission Control and open the live-camera view.**
```bash
cd frontend && npm run build && npm run start &
```
Open `http://localhost:4317/mission/live`. Confirm it shows `CAMERA SOURCE:
REAL_PHONE_CAMERA` and `ACTUATOR: VIRTUAL` — never a physical-hardware claim.

**7. Real Hybrid perception + state estimator.**
```bash
./build/debug/fsoc_live --source camera --camera-index 0 \
    --calibration configs/phone_camera.cfg --mode hybrid --tracker \
    --ai-model models/tiny_beacon_net.onnx
```
Mission Control's PERCEPTION panel now shows `MODE: HYBRID` with a real
`aiPresenceProbability`, and STATE ESTIMATOR shows a real `LOCK STATE`.

**8. Move the phone/camera.**
Physically pan the camera left/right across the beacon. Watch `panErrorDeg` change sign and
magnitude in real time, and `commandPanRateDegS` track it (`docs/PHONE_CAMERA_METRICS.md`'s
frozen sign convention).

**9. Try Manual Correction Assist.**
```bash
./build/debug/fsoc_live --source camera --camera-index 0 \
    --calibration configs/phone_camera.cfg --mode hybrid --tracker \
    --ai-model models/tiny_beacon_net.onnx --manual-assist
```
Follow the printed `REQUIRED CORRECTION` cue by hand; confirm the error shrinks on the next
line. This is manual-in-the-loop, not physical closed-loop control — say so explicitly.

**10. Hide the beacon briefly.**
Cover the monitor/beacon with your hand for under a second. Watch `lockState` go
`TRACKING -> COASTING` (`isPrediction=true` — the alpha-beta estimator bridging a real,
brief detection gap).

**11. Reveal it again.**
`lockState` returns to `TRACKING`, `isPrediction=false` — a fresh real measurement.

**12. Set the beacon in motion.**
On the beacon display, switch Motion to "Sinusoidal", then keep the camera roughly aimed at
it. Watch the pointing error and lock state respond to real, continuous target motion.

**13. Introduce a second bright light.**
Turn on a lamp, or open a second beacon-display window, near the tracked one. Record what
actually happens — a false lock, a source switch, or correct rejection. Do not expect this
to be solved (`docs/MVP_ABLATION.md` already discloses this as an open limitation for the
synthetic case; this is the real-camera version of the same experiment). This is one of the
most valuable real-camera validations precisely because it might fail informatively.

**14. Disconnect the camera mid-run.**
Physically unplug a USB camera (or close a continuity-camera connection). Confirm
`fsoc_live` prints a clean "too many consecutive frame read failures" message and exits —
no crash, no fabricated frames.

**15. Close the loop on claims.**
State explicitly, out loud, to whoever is watching: the sensing side (camera, perception,
state estimation, controller output) is fully real; the actuator side is honestly virtual.
See `docs/PHONE_CAMERA_METRICS.md` "Claim boundary" for the exact acceptable/unacceptable
phrasing.

## After the demo

Fill in `docs/PHONE_CAMERA_TEST_PLAN.md`'s manual validation table (M1–M14) with what you
observed, and `docs/PHONE_CAMERA_METRICS.md`'s "Real-camera metrics" table with the numbers
you measured. Optionally capture screenshots into `presentation_assets/phone_camera/`
(filenames suggested in the original milestone brief) — only capture states that actually
occurred.
