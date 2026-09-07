# Baseline Acceptance (v1_baseline)

Step 10 is the **evaluation layer**. It runs the *existing* v1 system across a fixed set of
named deterministic scenarios and checks acceptance gates that are **defined here, before
evaluation** — never derived from the run being scored. Step 10 changes no trajectory /
detector / PID / renderer / camera / telemetry / closed-loop code. If a scenario fails it
reports the failure; it does not loosen a gate or re-tune the controller.

Baseline controller (from Step 7, ADR-010, unchanged): **kp = 12, ki = 0, kd = 0** on both
axes, PID output limit = the camera actuator rate. Simulation is a **fixed 50 Hz** step
(`dt = 0.02 s`), fully deterministic (no RNG).

Camera / FOV: 640×480, hfov 20°, vfov 15° (half-FOV 10° / 7.5°), actuator rate limit 30°/s
per axis, tilt mechanical stops ±80°.

## How to run

```
cmake --preset debug && cmake --build --preset debug
./build/debug/step10_validation_smoke        # table + evidence + report; exit 0 iff PASS
ctest --preset debug -R fsoc_step10_tests     # unit checks on the suite
```

Artifacts are written to `generated/step10/` (git-ignored; never committed):

```
generated/step10/
├── VALIDATION_REPORT.md          # generated evidence (config, per-scenario checks, verdict)
├── static.csv  linear.csv  sinusoidal.csv  fov_edge.csv  saturation.csv
├── loss_reentry.csv  open_loop.csv  closed_loop.csv          # 34-column TelemetryRecord CSVs (27 core + 7 Stage-3 perception fields)
├── static_{initial,mid,final}.png   linear_{initial,mid,final}.png
├── sinusoidal_{t00,t05,t10,t15}.png   fov_edge_{initial,final}.png
├── saturation_{initial,final}.png     loss_{before,lost,reacquired}.png
└── open_vs_closed_{t00,t10}.png
```

## Global gates (every scenario)

| gate | rule |
|---|---|
| frame count | `> 0` |
| timestamps monotonic | `t[i+1] > t[i]` for all i |
| fixed-dt deviation | `max |t[i] − i·dt| ≤ 1e-9 s` |
| finite telemetry | every scalar / present optional is finite — **no NaN, no Inf** |
| command within PID limit | `max |command_rate| ≤ output_limit_rad_s` |
| applied within actuator limit | `max |applied_rate| ≤ camera max rate` |
| target-loss semantics | every lost frame: `command == {0,0}`, no `TrackingError`, `tracking_state == TargetLost`, all measurement optionals empty |
| deterministic replay | a second independent run gives bit-identical `BenchmarkMetrics` + `SimulationStepResult` sequence |

## Scenarios and gates

Error metrics are over frames with a `TrackingError`; percentile is nearest-rank
`ceil(0.95·N) − 1` (Step 8). "final" = the last tracking frame's total angular error.
PRD gate context: RMS image-centre error ≤ 10 % of the image half-diagonal (400 px) = 40 px.

### A — Static Acquisition *(why: coarse alignment from a pointing offset — the core product proof)*
Stationary target `{100, 6, 4}` m, camera pan = tilt = 0, 4 s.
- target detected in first frame
- detection fraction = 100 %
- initial total angular error `> 2°`
- final total angular error `< 0.05°`
- final centroid offset `< 2 px`
- final / initial error ratio `< 0.02`
- system remains `TRACKING`

### B — Slow Linear Tracking *(why: continual tracking of a mobile terminal)*
Linear target start `{100, −8, −3}` m, velocity `{0, 2.0, 0.8}` m/s, 10 s.
- detection fraction `≥ 95 %`
- RMS angular error `< 0.75°`
- P95 angular error `< 1.0°`
- final angular error `< 0.30°`
- lost frames `≤ 0`

### C — Sinusoidal Tracking *(why: nonlinear moving target)*
Sinusoid centre `{100, 0, 3}`, amplitude `{0, 22, 4}` m, frequency `{0, 0.12, 0.09}` Hz
(±12.4° Y swing), camera tilt initialised to the trajectory centre, 20 s.
- detection fraction `≥ 95 %`
- RMS angular error `< 1.0°`
- P95 angular error `< 1.0°`
- max angular error `< 1.5°`
- lost frames `≤ 0`
- late-window RMS / early-window RMS `≤ 1.5` (no uncontrolled error growth)

### D — Near-FOV-Edge Acquisition *(why: begin acquisition at the operational boundary)*
Stationary target `{100, 15.5, 8}` m — initial bearing ~8.8° pan (~88 % of the half-FOV) /
~4.5° tilt, **inside** the FOV; camera pan = tilt = 0, 4 s.
- target initially visible (in FOV)
- target initially detected
- first command points toward the target (RIGHT + ABOVE): `pixel_err_x > 0`, `pixel_err_y < 0`, `command_pan > 0`, `command_tilt > 0`
- no immediate target loss (all of the first 0.5 s detected)
- final / initial error ratio `< 0.05` (converges toward centre)
- final angular error `< 0.10°`
- final centroid offset `< 3 px`

### E — Actuator Saturation *(why: correct behaviour WHILE rate-limited)*
Stationary target `{100, 16, 12}` m — ~11° initial error, so `kp·e` exceeds the actuator
rate for several frames; 4 s. (The point is not zero saturation — it is correct behaviour
while saturated.)
- saturated frames occur (`> 2`)
- max command rate `≤ PID output limit`
- max applied rate `≤ camera actuator limit`
- saturation flags present in telemetry
- system comes off the limit (final frame not saturated)
- tracking error reduced during and after saturation (`e[N/4] < e[0]` and `e[N/2] < e[N/4]`)
- final angular error `< 0.05°`

### F — Target Loss and Re-entry *(why: explicit lost-target semantics + safe recovery)*
Aggressive sinusoid, amplitude `{0, 42, 4}` m, frequency `{0, 0.30, 0.10}` Hz: the target
leaves the FOV, the loop reports `TargetLost`, and the target naturally returns. Baseline
policy: PID reset, zero command, camera holds. No search mode. 8 s.
- lost frames occur (`> 0`)
- a `TRACKING → TARGET LOST` transition occurs
- a `TARGET LOST → TRACKING` transition occurs (natural reacquire)
- command exactly zero while lost
- camera pose held across consecutive lost frames
- first post-reacquisition control resumes (angular error decreases within ~0.8 s)

*(This scenario deliberately has low detection fraction and high RMS — it validates the
loss/recovery semantics, not tracking quality.)*

### G — Open vs Closed Loop *(why: prove the control system adds value)*
Same nominal sinusoid as C, run `control_enabled = false` then `true`, 20 s each.
- closed detection fraction `>` open detection fraction
- closed RMS `<` open RMS
- closed P95 `<` open P95
- closed lost frames `<` open lost frames
- RMS improvement factor `≥ 3×` *(a robust bound; the baseline achieves ~11.8×)*
- detection improvement `≥ 20` percentage points *(baseline: +42.6 pts)*

## Overall verdict

`BASELINE ACCEPTANCE: PASS` **only if every check of every scenario passes.** The Step-10
tests include a mandatory failure check (`test_failure_check_is_real`) proving the evaluator
reports FAIL when a gate is not met — it is not a decorative always-green harness.
