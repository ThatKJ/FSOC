# Stitch → Application Implementation Map

Source of truth: the **FSOC** Stitch project (`projects/4731868309872967927`,
"Orbital Precision System" design system). 9 generated screens + 1 uploaded
reference image (`image.png`, not a screen). Every screen is implemented; each
maps 1:1 to a route.

Design tokens: `frontend/DESIGN_SYSTEM.md`. Data contract:
`docs/18_FRONTEND_DATA_CONTRACT.md`. All telemetry is real C++ `fsoc_demo`
output (LOCAL ENGINE MODE) or its checked-in deterministic replay (REPLAY MODE).

---

## Global shell (present on every Stitch screen)

| Stitch element | Component | Notes |
|---|---|---|
| fixed header `h-[48px]` | `components/shell/TopBar.tsx` | FSOC title + SIH26169 badge; centre scenario pill; SIM clock; Uplink indicator; avatar |
| scenario pill (centre) | `components/shell/ScenarioMenu.tsx` | **functional** — click to switch the active `DemoScenario` |
| left rail `w-[64px]` | `components/shell/NavRail.tsx` | 9 icon links, active = `text-primary border-r-2 border-primary`, `usePathname()` |
| `<main pl-[64px] pt-[48px]>` | `components/shell/AppShell.tsx` | |
| — (added) | `components/shell/SourceToggle.tsx` | LOCAL ENGINE ⇆ REPLAY toggle (functional addition; documented deviation) |

Icon mapping (Material Symbols → Lucide): `grid_view`→`LayoutGrid`,
`target`→`Crosshair`, `center_focus_strong`→`ScanEye`, `language`→`Globe`,
`query_stats`→`Activity`, `account_tree`→`Network`, `speed`→`Gauge`,
`fact_check`→`ClipboardCheck`, `schema`→`Workflow`, `person`→`User`,
`play_arrow`→`Play`, `arrow_forward`→`ArrowRight`, `chevron_right`→`ChevronRight`,
`download`→`Download`, `check`/`done`→`Check`, `warning`→`TriangleAlert`,
`trending_up`→`TrendingUp`, `info`→`Info`, `skip_previous/next`→`SkipBack/Forward`,
`pause`→`Pause`, `refresh`→`RotateCcw`.

---

## 1. Overview  →  `/`   (Stitch screen `a820e1b2…`)

- **Components:** `app/page.tsx`, `TrackingFeedLive` (compact), `PointingErrorChartLive`, hero block, camera chips, status strip, pipeline breadcrumb.
- **Functionality:** "Enter Mission Control" → `/mission`; "Run Demo Scenario" → `runScenario(scenario)` + `/tracking` (auto-plays). Right viewport is the live optical feed. State / Error / Pan-Tilt / pointing-error chart update with playback.
- **Data:** `DemoSnapshot` (state, tracking.totalErrorDeg, tracking.errorX/YPx, camera.pan/tiltDeg); scenario meta; constants (FOV, sim rate).

## 2. Mission Control  →  `/mission`   (`66afbdd3…`)

- **Components:** `app/mission/page.tsx`, `TrackingFeedLive`, `TelemetryStream` (right rail, 320px), `PointingErrorChartLive`, `EventLog`, `MiniTransport`.
- **Functionality:** optical feed + Telemetry Stream + pointing-error trace + event log, **all synchronized to one `simTime`**. "Abort Tracking" → `pause()` + `reset()`. Mini transport in the status strip.
- **Data:** full `DemoSnapshot` (camera pose/rates, tracking error deg+px, target truth); derived `SimEvent[]`.

## 3. Optical Tracking  →  `/tracking`   (`1f957533…`, project cover)

- **Components:** `app/tracking/page.tsx`, `TrackingFeed` / `TrackingFeedLive`, `MiniTransport`.
- **Functionality (TrackingFeed contract):** centre crosshair at the optical centre; detected reticle positioned from `detection.xPx/yPx` (mapped through the 640×480 sensor rect); centre→detection **error vector**; reticle + vector **hidden on `TARGET_LOST`**, `TARGET LOST` banner shown; **natural reacquisition** returns the reticle; `RATE LIMIT` chip when `control.*Saturated`; bottom HUD (STATE / ERROR deg+px / PAN TILT t=) + pointing-error sparkline; optical camera `<video>` (`/demo/fsoc-tracking*.mp4`, real Step-9 visualizer output) → still frame → void fallback.
- **Deviation:** Stitch shows "120 FPS"; we show the true fixed **50 Hz**. Stitch has no visible transport; a `MiniTransport` is added (playback must work).

## 4. Spatial / 3D View  →  `/world`   (`6b3fca4d…`)

- **Components:** `app/world/page.tsx`, `components/world/WorldCanvas.tsx` → `WorldScene.tsx` (React Three Fiber, `ssr:false`), `MiniTransport`.
- **Functionality:** telemetry-driven scene — camera terminal rotates with `camera.pan/tiltDeg`; target marker at `target.position` (world→scene map, +X fwd/+Y right/+Z up); line of sight (hidden on loss); FOV frustum (±hfov/2, ±vfov/2); trajectory path from all frames; world axes X(red)/Y(mint)/Z(cyan). View buttons WORLD / CAMERA / TOP / SIDE lerp the camera; OrbitControls for orbit/pan/zoom. Right telemetry panel (position, velocity, camera attitude, LOS error).
- **Deviation:** Stitch's spatial panel is a static CSS mock; this is a real 3-D scene of the same elements. Velocity panel shows LINEAR speed + |Y rate| (Stitch's "ANGULAR" has no direct telemetry field).

## 5. Telemetry  →  `/telemetry`   (`211aee61…`)

- **Components:** `app/telemetry/page.tsx`, `MetricStat` ×4, `ChartCell` ×6, `TimeSeriesChart`, `PlaybackControls`.
- **Functionality:** metrics bar + 2×3 synchronized chart grid + full transport (scrubber with event markers, skip/play/pause, 0.5x/1x/2x, SIM/SYS time). Playhead = shared `simTime`. Loss/reacq event markers on charts.
- **Chart mapping (real signals):** ① Total Angular Error (deg) ② Pan/Tilt Error Separation (deg) ③ Command vs Applied Rate — PAN (deg/s) ④ Command vs Applied Rate — TILT (deg/s).
- **Deviations (no such signal in the frozen 27-col telemetry contract):**
  - "Motor Current Draw" → **Detection Centroid Offset (px)** (`hypot(errorXPx, errorYPx)`).
  - "Sensor Fusion Confidence" → **Detection Error vs Truth (px)** (`detectionErrorPx`).
  - "Command Latency 12.4ms" metric → **Frame Interval 20.0 ms (fixed 50 Hz)**.
  - "Detection Rate 100.00%" / "RMS 0.546°" use the validated Step-10 numbers for the active scenario.

## 6. Scenarios  →  `/scenarios`   (`1f73cb9b…`)

- **Components:** `app/scenarios/page.tsx`, `Button`.
- **Functionality:** 8-module list (as in Stitch) + detail pane (description, validated Step-10 parameters, kinematic preview). **Compare** → `/benchmarks`; **Load** → `setScenario`; **Run** → `runScenario` + `/mission` (auto-plays).
- **Deviation:** Stitch lists 8 modules; **5 map to `fsoc_demo` DemoScenarios and are RUNNABLE** (static, sinusoidal, loss, open, closed). The other 3 — SLOW LINEAR TRACKING, NEAR FOV EDGE, ACTUATOR SATURATION — are **Step-10 validation-only** (no CLI preset); they show validated metrics and a "View in Validation" link, `Run` disabled. Stitch's fictional params (`INIT JITTER 0.5 μrad`, `BACKGROUND IL`) are replaced with the real validated result (detection %, RMS, lost frames, frames).

## 7. Benchmarks  →  `/benchmarks`   (`6d827bb1…`)

- **Components:** `app/benchmarks/page.tsx`, `TimeSeriesChart`, `useScenarioFrames` (fetches `open` + `closed`).
- **Functionality:** OPEN LOOP vs CLOSED LOOP panels with the **validated** numbers (`57.4%` / `6.4549°` / `426 lost` vs `100%` / `0.5461°` / `0 lost`), `11.8×` gain callout, `+42.6 pts` detection. Bottom "Synchronized Error vs Time" overlays the two **real** telemetry runs on one axis. Divergence profilers drawn from real `totalErrorDeg`.
- **No invented "AI improvement" numbers** — all from `lib/baseline/constants.ts` (`BENCHMARK`, sourced from docs/17 + Step 10).

## 8. Validation  →  `/validation`   (`fe4329dc…`)

- **Components:** `app/validation/page.tsx`.
- **Functionality:** "7 / 7 SCENARIOS PASSED", results table (row-click selects), detail panel (metrics grid + verdict checklist + note), **EXPORT LOG** downloads a real CSV.
- **Data:** the **actual Step-10 results** from `docs/16_BASELINE_ACCEPTANCE.md` / the generated `VALIDATION_REPORT.md` — 7 scenarios: Static Acquisition, Slow Linear Tracking, Sinusoidal Tracking, Near-FOV-Edge Acquisition, Actuator Saturation, Target Loss & Re-entry, Open vs Closed Loop.
- **Deviations:** Stitch shows fictional scenario names + **µrad** + "LOST (ms)"; we show the **real** Step-10 scenario names, **degrees** (as reported in docs/16), and **LOST (frames)**. Loss/Re-entry has 107 lost frames — shown with a caution glyph and a note that this is **validated loss-semantics behaviour, PASS**, not a failure.

## 9. Architecture  →  `/architecture`   (`49b28e01…`)

- **Components:** `app/architecture/page.tsx`.
- **Functionality:** SYS_ARCH meta (topology, 50 Hz, PID kp=12/ki=0/kd=0), SIGNAL INTEGRITY diagnostics (wired to `detectionErrorPx`, `totalErrorDeg`, applied rates), the pipeline node graph (Target Trajectory → Camera Geometry → Optical Frame │ **PIXEL MEASUREMENT BOUNDARY** │ Beacon Detector ← Tracking Error ← PID Controller), floating **Actuator State** panel wired to `camera.pan/tiltDeg` + saturation.
- **Deviation:** the animated multi-segment feedback-loop SVGs / flow-pulse markers in Stitch are simplified to static connectors; node layout, labels and the boundary callout are faithful. "SNR: 42.8 dB", "LAT: 0.8ms", "PWR: 12.4W" replaced with live-derived equivalents.

---

## Cross-cutting functional coverage

| Requirement | Where |
|---|---|
| all 9 routes load, current route visible, back/forward | `NavRail` + Next App Router |
| scenario selection (static/sinusoidal/loss/open/closed) | `ScenarioMenu`, `/scenarios`, `SimulationProvider` |
| playback start/pause/resume/reset, scrub, 0.5x/1x/2x | `PlaybackControls`, `MiniTransport`, `SimulationProvider` (RAF clock) |
| TRACKING / TARGET_LOST / RATE LIMIT states | `TrackingFeed`, `TelemetryStream`, everywhere via `DemoSnapshot` |
| charts update with playback, synchronized sim time, tooltips, event markers | `TimeSeriesChart` + `playhead={simTime}` + `markers` |
| reticle tracks detection, hides on loss, returns on reacq | `TrackingFeed` (`placeReticle`) |
| event log from telemetry transitions (never random) | `lib/simulation/events.ts`, `EventLog` |
| open vs closed data separation | `useScenarioFrames("open")` + `("closed")` |
| deterministic replay + LOCAL ENGINE mode, identical visuals | `/api/simulation/[scenario]`, `SourceToggle` |
| **no `Math.random`** for any simulation/telemetry state | grep-clean; only `deriveEvents` / real frames |
| invalid scenario handled | API returns 400; `SimulationProvider` surfaces `status:"error"` |

## Intentional deviations from Stitch (summary)

1. Icons: Lucide instead of Material Symbols (closest-glyph mapping above).
2. `SourceToggle` added to the header (engine ⇆ replay) — functional, matches shell styling.
3. `MiniTransport` added to viewport-only screens (Tracking, World, Mission strip) — Stitch shows a static viewport; playback must work.
4. Telemetry: 2 of 6 charts substituted (no motor-current / sensor-fusion signal exists); "Command Latency" → "Frame Interval".
5. Scenarios: 3 of 8 modules are validation-only (no `fsoc_demo` preset); fictional parameters replaced with real validated results.
6. Validation: real Step-10 scenario names + degrees + "LOST (frames)" instead of fictional names + µrad + "LOST (ms)".
7. World: a real R3F scene instead of the CSS-mock spatial panel; "ANGULAR velocity" → "|Y rate|".
8. Architecture: static connectors instead of animated flow SVGs; hardware-diagnostic numbers derived from live telemetry.
9. Numbers shown are the **validated** engine/Step-10 values, not the Stitch mock values (e.g. RMS 0.5461° not "0.546°", detection 57.4%/100%).
