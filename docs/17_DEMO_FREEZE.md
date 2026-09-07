# Demo Freeze (Step 11)

The `v1_baseline` tag is **created and pushed to `origin`** (it points at the
merged Step‑10 baseline, commit `20c028c`); that validated baseline is **frozen**
and the tag is never moved. Step 11 adds a presentation / packaging layer only —
`DemoScenario` presets, the `DemoSnapshot` view model, and the `DemoSession`
runner — plus the `fsoc_demo` CLI. It changes **no** validated algorithm:
geometry, camera projection, FOV math, trajectory equations, renderer, detector,
`TrackingError`, the PID law, the baseline PID gains (`kp=12, ki=0, kd=0`), the
`SimulationRunner` execution order, and the Step-10 acceptance thresholds are all
untouched. See `docs/16_BASELINE_ACCEPTANCE.md` for the frozen gates and
`docs/18_FRONTEND_DATA_CONTRACT.md` for the snapshot field contract.

## Demo scenarios

| CLI token | `DemoScenario` | reuses (Step 10) | control | duration |
|---|---|---|---|---|
| `static` | `StaticAcquisition` | Static Acquisition — stationary `{100,6,4}` m | closed | 4 s / 200 frames |
| `sinusoidal` | `SinusoidalTracking` | Sinusoidal Tracking — center `{100,0,3}`, amp `{0,22,4}`, freq `{0,0.12,0.09}` Hz | closed | 20 s / 1000 frames |
| `loss` | `LossReacquisition` | Target Loss and Re-entry — amp `{0,42,4}`, freq `{0,0.30,0.10}` Hz | closed | 8 s / 400 frames |
| `open` | `OpenLoop` | Open vs Closed Loop, `control_enabled = false` | **open** | 20 s / 1000 frames |
| `closed` | `ClosedLoop` | Open vs Closed Loop, `control_enabled = true` | closed | 20 s / 1000 frames |

`open` and `closed` use **identical trajectory parameters**; only
`control_enabled` differs (verified by `test_open_closed_same_trajectory`).
Parameter values are copied verbatim from `src/validation.cpp` and are **not**
retuned.

## Expected demo results (fixed, deterministic)

| scenario | detection | RMS ang err | P95 | max | lost |
|---|---|---|---|---|---|
| `static` | 100.0 % | 0.4691° | 0.2885° | 4.1275° | 0 / 200 |
| `sinusoidal` | 100.0 % | 0.5461° | 0.7899° | 0.7982° | 0 / 1000 |
| `loss` | 73.2 % | 4.9328° | 8.8284° | 9.9692° | 107 / 400 |
| `open` | 57.4 % | 6.4549° | 9.8112° | 10.1916° | 426 / 1000 |
| `closed` | 100.0 % | 0.5461° | 0.7899° | 0.7982° | 0 / 1000 |

`static` acquires 4.13° → 0.0000°. `closed` is identical to `sinusoidal` (same
trajectory, controller on). Wall-clock FPS printed by the CLI is informational
only — the simulation is always fixed 50 Hz.

## Teammate macOS workflow

No machine-specific paths are hardcoded anywhere; CMake discovers OpenCV via
`find_package`.

```bash
# 1. clone
git clone <repo-url>
cd FSOC

# 2. dependencies (Apple Silicon or Intel; Homebrew picks the right prefix)
brew install cmake ninja opencv

# 3. configure
cmake --preset debug

# 4. build
cmake --build --preset debug

# 5. run the full test suite (must be 11/11)
ctest --preset debug

# 6. baseline acceptance — must end "STEP 10 BASELINE ACCEPTANCE: PASS"
./build/debug/step10_validation_smoke

# 7. run the demo scenarios
./build/debug/fsoc_demo --help
./build/debug/fsoc_demo static
./build/debug/fsoc_demo sinusoidal
./build/debug/fsoc_demo loss
./build/debug/fsoc_demo open
./build/debug/fsoc_demo closed

# CLI options:
#   --mode <mode>          classical (default) | ai | hybrid (Stage-3 ONNX perception;
#                          falls back to classical with a warning if the model is missing)
#   --duration <seconds>   shorten/lengthen the run (demo knob only; still 50 Hz)
#   --csv <path>           write the 34-column telemetry CSV
#   --quiet                summary only, no per-frame lines

# 8. one-shot reproducible bundle (validation + demos + visualization evidence)
make demo            # or: ./scripts/run_baseline_demo.sh
```

## Generated evidence

All generated artifacts live under `generated/` (git-ignored — never committed):

| path | produced by | contents |
|---|---|---|
| `generated/step10/` | `step10_validation_smoke` | `VALIDATION_REPORT.md` + 8 per-scenario CSV + 19 annotated PNG |
| `generated/demo/` | `fsoc_demo --csv ...` / `make demo` | `static_demo.csv`, `sinusoidal_demo.csv` (34-col telemetry: 27 core + 7 Stage-3 perception fields) |
| `generated/step9/` | `step9_visualization_smoke` | 28 annotated camera-view PNG frames (+ optional `.mp4`) |

Regenerate everything at once with `make demo`.

## What is NOT in Step 11

No web frontend, React, Next.js, Three.js, WebSocket, HTTP server, JSON library,
UKF, MPC, Kalman filtering, turbulence / vibration / Zernike models, new
detector, new PID gains, search/reacquisition mode, threads, or async. Those are
post-baseline work and must arrive as swappable implementations behind the
existing stable interfaces.
