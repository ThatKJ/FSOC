# Phone Camera-in-the-Loop — Architecture, Metrics, and Boundaries

This document covers the **Mobile Phone Camera-in-the-Loop** milestone: extending FSOC from
a synthetic camera + simulated actuator to a **real mobile-phone camera + real perception +
real state estimation + real controller + an honestly-virtual actuator**. It does NOT
replace `docs/MVP_METRICS.md` (the synthetic-simulation metrics) — the two are kept
strictly separate, per this milestone's own rule.

## What is real vs. what is virtual

```
REAL TARGET / BEACON  ->  MOBILE PHONE CAMERA  ->  REAL VIDEO FRAME
   -> Hybrid Perception -> State Estimator -> Prediction -> Controller
   -> VirtualPanTiltActuator -> REAL CONTROL COMMAND TELEMETRY
```

| Stage | Real or virtual | Why |
|---|---|---|
| Camera / frames | **REAL** | `OpenCVCameraFrameSource` (`cv::VideoCapture`), a genuine phone/webcam device or network stream |
| Preprocessing | **REAL** | Actual grayscale/resize of the actual captured pixels |
| Classical + AI perception | **REAL** | The exact same `BeaconDetector` / `AiBeaconDetector` / `resolve_perception()` (Safe Hybrid, ADR-018) the simulation uses, run on real frames |
| State estimation (alpha-beta, coasting, reacquisition) | **REAL** | The exact same `TargetTracker` (ADR-019), run on real measurements |
| Controller output (pan/tilt rate command) | **REAL** | The exact same `PIDController`, run on a real image-derived tracking error |
| Actuator | **VIRTUAL** | `VirtualPanTiltActuator` integrates the real command into a bookkeeping angle. It drives **no physical hardware**. See "Claim boundary" below. |

**Acceptable claims** (from this milestone): "real-camera-in-the-loop prototype", "physical
phone-camera frames processed by the FSOC perception stack", "Hybrid perception validated on
live phone-camera input", "real-camera target-loss / reacquisition demonstrated",
"controller commands generated from real image-derived pointing error".

**Not acceptable, until physical hardware exists**: "physical pan/tilt tracking", "hardware
closed-loop FSOC terminal", "physical actuator validation", "real FSOC communication",
"laser link established". `fsoc_live`'s own startup banner and every JSON telemetry frame
print `CAMERA SOURCE = REAL_PHONE_CAMERA` / `ACTUATOR = VIRTUAL` explicitly so this
distinction cannot be silently lost in a demo or a screenshot.

## Architecture

### FrameSource — the interchangeable frame boundary

```cpp
class FrameSource {
    virtual bool open() = 0;
    virtual bool read(Frame& frame) = 0;
    virtual void close() = 0;
    virtual FrameSourceInfo info() const = 0;
};
```

`fsoc/frame_source.hpp`. One class, `OpenCVCameraFrameSource`, backs BOTH a native camera
index (`--camera-index N`) and a network URL (`--camera-url URL`) — `cv::VideoCapture`
already exposes both through the same `open()`/`read()` surface, so a second
"NetworkCameraFrameSource" class would just duplicate the wrapper. `FrameSource` is a pure
I/O boundary: it depends on nothing from the perception stack, and the perception stack
depends on nothing from it — `LiveTrackingSession::process_frame()` takes a `Frame` and a
`FrameSourceInfo` by value/reference, never a `FrameSource*`.

### Why the existing perception stack needed almost no changes

The audit for this milestone found that `BeaconDetector::detect()`, `AiBeaconDetector::detect()`,
`resolve_perception()`, `compute_tracking_error()`, `TargetTracker::update()`, and
`PIDController::update()` were **already** pure functions of pixels/angles with no coupling
to world truth, the renderer, or `SimulationRunner`. Only two real risks existed, both fixed
before any live-camera code was written:

1. **`AiBeaconDetector`'s fixed `kInputStride`.** Its heatmap-to-pixel decode assumes the
   frame handed to it is exactly the network's native resolution (640×480 by default). A raw
   phone frame at any other resolution must be resized to exactly that size first — see
   `preprocess_live_frame()` (`fsoc/live_preprocessing.hpp`).
2. **Sensing-camera scale.** `PanTiltCamera::fx_px()` = `(width_px/2) / tan(hfov/2)` — it
   scales with `width_px` for a fixed field of view. The sensing-reference `PanTiltCamera`
   `LiveTrackingSession` builds for `compute_tracking_error()` therefore uses the
   **preprocessed frame's** width/height (640×480 by default), not the phone's raw capture
   resolution, with the calibration's `hfov`/`vfov` carried over unchanged (field of view is
   resolution-independent). Getting this backwards would silently scale every angular error
   by the raw/preprocessed size ratio. See `sensing_camera_config()` in
   `src/live_tracking_session.cpp`.

### LiveTrackingSession — the real-camera counterpart to SimulationRunner

Mirrors `SimulationRunner::step()`'s control-path order exactly:

```
preprocess -> classical (+ AI) detect -> resolve_perception
           -> [tracker if enabled] -> compute_tracking_error
           -> control (or loss/open-loop policy) -> virtual actuator step
```

`LiveFrameResult` carries **no** `target_truth`, `observation`, or `target_visible` field —
there is no ground truth for a real camera frame, so none is fabricated or leaked (see
`fsoc/live_tracking_session.hpp`).

### Camera calibration (`fsoc/live_camera_calibration.hpp`)

Deliberately **not** a photogrammetry suite. A pinhole model declared by four numbers
(`width_px`, `height_px`, `hfov_deg`, `vfov_deg`), stored in a dependency-free `key=value`
text file — the C++ core links no JSON library, and a four-field calibration doesn't
justify adding one. Two ways to produce a calibration file:

- **Manual** (`fsoc_camera_calibrate --manual ...`): type in the phone's real spec-sheet FOV.
- **From a known object** (`fsoc_camera_calibrate --from-object ...`): angular-substitution
  estimate from one known-size object at a known distance
  (`estimate_hfov_deg_from_known_object`), with `vfov` derived from `hfov` assuming square
  pixels (`estimate_vfov_deg_from_hfov`). A first-order approximation (no lens-distortion
  correction) — adequate for coarse alignment, not a substitute for a real checkerboard
  calibration.

**The synthetic simulation's `CameraConfig` (hfov 20°, vfov 15°) is never assumed to equal a
phone's real FOV.** A calibration file is required before `fsoc_live` will run.

### Virtual actuator (`fsoc/virtual_actuator.hpp`)

`VirtualPanTiltActuator` consumes the same `ControlCommand`-shaped angular rate the
simulation's `PanTiltCamera::step()` consumes, integrates it into a bookkeeping angle with
rate saturation and an optional soft tilt-travel limit — but it drives nothing and, just as
importantly, it does **not** feed back into how the next real frame's pixel error is
interpreted (the phone's actual physical orientation is controlled by a human, not by this
class). Deliberately a separate class from `PanTiltCamera` rather than reusing it: reuse
would blur exactly the boundary this milestone exists to keep sharp. Every telemetry frame
labels it `ACTUATOR_TYPE = VIRTUAL`.

### Manual Correction Assist (`fsoc_live --manual-assist`)

Turns the real tracking error into a human-readable cue, e.g.:

```
REQUIRED CORRECTION   PAN RIGHT -> 3.4 deg      TILT UP 0.6 deg
```

using the same frozen sign convention `PIDController`/`PanTiltCamera` already rely on
(`fsoc/tracking_error.hpp`): a beacon right of centre needs a rightward pan to centre it, a
beacon above centre needs an upward tilt. This is **manual-in-the-loop** — a human physically
moves the phone — never "closed-loop hardware control."

## Live telemetry JSON schema

`fsoc_live` overwrites `<live-out>/telemetry.json` after every processed frame (default
`generated/live/`, gitignored — this is runtime state, not a build artifact) and
`<live-out>/frame.jpg` with the latest raw frame. Fields (all camelCase):

| Field | Meaning |
|---|---|
| `frameIndex`, `timestampS`, `dtS` | Frame counter, source-clock time, measured wall-clock interval since the previous frame (a real camera has no fixed timestep) |
| `cameraSource` | Always `"REAL_PHONE_CAMERA"` |
| `actuatorType` | Always `"VIRTUAL"` |
| `sourceKind`, `sourceBackend`, `sourceDescription` | From `FrameSourceInfo` — e.g. `REAL_CAMERA`, `AVFoundation`, `camera index 0` |
| `rawWidthPx`/`rawHeightPx`, `preprocessedWidthPx`/`preprocessedHeightPx` | What the camera actually negotiated vs. what the detector actually measured pixels in |
| `perceptionMode`, `perceptionSource`, `classicalDetected`, `aiCandidateDetected`, `aiPresenceProbability` | `PerceptionDiagnostics`, unchanged from the simulation's own contract |
| `targetDetected`, `detectedXPx`/`detectedYPx` | The control-facing detection, if any |
| `pixelErrorXPx`/`pixelErrorYPx`, `panErrorDeg`/`tiltErrorDeg`/`totalErrorDeg` | `TrackingError`, converted to degrees at the UI boundary (matches the simulation's own degrees-only-at-the-boundary convention) |
| `lockState`, `trackerConfidence`, `isPrediction` | `TrackedState` (P0-v2 estimator), unchanged |
| `controlEnabled`, `commandPanRateDegS`/`commandTiltRateDegS` | The PID's real output |
| `virtualPanDeg`/`virtualTiltDeg`, `virtualPanSaturated`/`virtualTiltSaturated` | `VirtualActuatorState` — bookkeeping only |

No JSON library is linked into the C++ core (the existing telemetry system is CSV, not
JSON), so this is hand-serialized in `apps/fsoc_live.cpp`. This schema is the contract; the
serialization code must match it exactly.

## Mission Control transport

`/api/simulation/:scenario` runs a **finite** scenario to completion and returns every frame
as one JSON array for the frontend to scrub through — that pattern does not fit a real
camera session, which has no natural end. Instead:

```
fsoc_live (long-running process, started by YOU)
    -> overwrites generated/live/telemetry.json + frame.jpg after every frame
    -> GET /api/live-camera        (Next.js route, polled every 500ms)
    -> GET /api/live-camera/frame  (Next.js route, polled every 500ms, cache-busted)
    -> frontend/app/mission/live/page.tsx (reuses the existing Panel/KeyValueRow primitives)
```

This is an honest **polling snapshot read**, not a push/streaming connection — latency is
bounded by the poll interval (≤500ms) plus whatever `fsoc_live`'s own frame-processing rate
is, not sub-frame real-time. `/mission/live` is reachable by direct link, not one of the 9
fixed Stitch nav screens (`lib/nav.ts` — see `STITCH_IMPLEMENTATION_MAP.md`), so the existing
navigation rail is unchanged.

## Real-camera metrics

**These require a physical phone/camera and OS camera permission on your own machine — they
cannot be measured from this automated session** (see "What could not be measured" below).
Record them here after running `fsoc_camera_view` / `fsoc_live` yourself:

| Metric | How to measure | Value |
|---|---|---|
| Actual negotiated resolution | `fsoc_camera_probe` output | _(fill in)_ |
| Effective camera FPS | `fsoc_camera_view` stdout (`effective_fps=`) | _(fill in)_ |
| Frame capture + preprocess + perception latency | Add `--seconds`-bounded timing around `process_frame()` if needed, or the wall-clock `dtS` field in `telemetry.json` | _(fill in)_ |
| Centroid jitter on a stationary target | Stddev of `detectedXPx`/`detectedYPx` over N frames of a static beacon | _(fill in)_ |
| Detection availability | Fraction of frames with `targetDetected == true` over a session | _(fill in)_ |
| Short-occlusion recovery | Manual: cover/uncover the beacon, confirm `lockState` goes `TRACKING -> COASTING -> TRACKING` (see `docs/PHONE_CAMERA_TEST_PLAN.md`) | _(fill in)_ |
| False-lock observations under clutter | Manual: introduce a second bright source, record what `perceptionSource`/`lockState` do | _(fill in)_ |

**No pointing "accuracy" number is reported** — there is no calibrated real-world reference
target position in this milestone, and fabricating one would violate this milestone's core
rule.

## Hardware-ready actuator interface (future)

Not implemented now — documented so a future contributor knows what shape to build to.

```
PanTiltActuator (interface)
├── SimulatedPanTiltActuator   (existing: PanTiltCamera::step(), simulation only)
├── VirtualPanTiltActuator     (this milestone: honest bookkeeping, no hardware)
└── SerialPanTiltActuator      (FUTURE — not implemented, no fake ACKs, no Arduino claim)
```

A future serial/hardware adapter should accept the exact same `ControlCommand`-shaped
`(pan_rate_rad_s, tilt_rate_rad_s)` this milestone's `VirtualPanTiltActuator` and the
simulation's `PanTiltCamera::step()` already accept — no interface change needed upstream of
the actuator.

**The important mismatch to solve then, not now:** this simulation's controller emits an
**angular rate** command; a cheap hobby servo wants a **position** command. The likely
adapter-side conversion:

```
pan_target += pan_rate * dt
tilt_target += tilt_rate * dt
# then clamp to the physical servo's real travel limits
```

This conversion belongs entirely inside the future `SerialPanTiltActuator`, not upstream of
it — the controller, tracker, and perception stack should not need to change when real
hardware arrives.

## Known limitations

- The AI detector's heatmap decode is calibrated for 640×480 input; real frames are always
  resized to that before detection (see "Architecture" above). This is adequate for coarse
  alignment but is a real information loss for a phone that captures at much higher
  resolution.
- Camera calibration is a single-FOV pinhole approximation with no lens-distortion
  correction (Phase 8's own explicit scope boundary).
- The Mission Control live view is a polling read, not a low-latency stream (see "Mission
  Control transport").
- No physical actuation exists. Every "correction" is either bookkeeping
  (`VirtualPanTiltActuator`) or a cue for a human to act on (Manual Correction Assist).
- What could not be measured from this automated coding session: opening a real camera
  device requires interactive OS permission approval (macOS TCC) and visibly activates the
  camera indicator light — an automated agent session should not trigger that unattended.
  `fsoc_camera_probe`, `fsoc_camera_view`, and `fsoc_live` are built, unit-tested against
  fabricated frames (`tests/live_tracking_session_tests.cpp` and friends), and ready to run,
  but the actual live-hardware verification in `docs/PHONE_CAMERA_TEST_PLAN.md`'s "Manual
  validation" section is intentionally left for you to run yourself.
