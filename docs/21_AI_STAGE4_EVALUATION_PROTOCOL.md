# 21 — Stage-4 Evaluation Protocol (FROZEN)

**Status:** frozen before the final Stage-4 benchmark was executed. This document
is written and committed to *before* looking at any Stage-4 result. Nothing in
this file may be changed because of an observed number. If a genuine defect is
found in the protocol after execution, it must be fixed in a *new*, separately
reviewed revision — never silently, never to move a result.

```
STAGE4_PROTOCOL_FROZEN = YES
```

## 1. Purpose

Stage 4 is an evaluation stage, not a design stage. It objectively compares the
three already-frozen Stage-3 perception modes —
`PerceptionMode::{Classical, AI, Hybrid}` — under deterministic degraded optical
conditions, using the untouched Stage-1/2/3 artifacts:

- classical detector: `include/fsoc/detector.hpp` / `src/detector.cpp` (unchanged)
- AI detector: `models/tiny_beacon_net.onnx`, presence threshold **0.95** (unchanged)
- Hybrid policy: ADR-018, `agreement_radius_px = 8.0` (unchanged)
- PID: `kp=12, ki=0, kd=0`, actuator limits, FOV, `dt=0.02s` (all unchanged)

No algorithm is modified as a result of this stage. Everything evaluated here is
frozen *before* the numbers exist.

## 2. Two evaluations, two questions

**A. Common-frame perception benchmark** — isolates detector/perception quality
from control-loop dynamics: the exact same deterministic frame is scored by
Classical, AI, and Hybrid. Answers "how good is each detector, by itself, on
this exact image?"

**B. Closed-loop tracking benchmark** — runs the real `PanTiltCamera` + PID
+ trajectory loop end-to-end per mode. Camera trajectories are *expected* to
diverge across modes once control decisions differ; frames are **not** forced
to stay identical after that point. Answers "what actually happens to the
gimbal under this condition, per mode?"

## 3. Deterministic frame construction

### 3.1 Common-frame benchmark
Uses the existing, frozen, dataset-generation tool `fsoc::ai::AiFrameSynthesizer`
(`include/fsoc/ai_frame_synth.hpp`, Stage 1) directly — this is exactly the tool's
documented purpose ("training AND EVALUATION of the learned beacon detector").
Beacon position, presence/absence, and every degradation parameter for a given
sample are drawn entirely from `std::mt19937_64` seeded by that sample's
deterministic seed; no wall-clock or platform-dependent randomness anywhere.

### 3.2 Closed-loop benchmark
`AiFrameSynthesizer` draws its own **random** beacon position per sample — it has
no way to place the beacon at a caller-chosen pixel, so it cannot be used
directly to render a frame consistent with a live trajectory/camera pose. The
closed-loop benchmark therefore:

1. Renders the physically-correct, truth-positioned clean frame through the
   **frozen, unmodified** `SyntheticCameraRenderer` (Step 4), using a per-scenario
   `RendererConfig` (only `beacon_peak_intensity` / `background_intensity` /
   `beacon_sigma_px` vary — all three are already-documented configurable fields
   of that frozen struct; no renderer code is touched).
2. Applies a **new, additive, Stage-4-only** post-process,
   `fsoc::stage4::apply_degradation()` (`include/fsoc/stage4_degradation.hpp`),
   to that already-correctly-positioned clean/background-only frame: background
   gradient + vignette, star-like clutter, an optional single bright/twin
   distractor point, one optical blur operator, shot + read noise, and hot/dead/
   salt pixels — the same *conceptual* operations `AiFrameSynthesizer` itself
   applies, in the same order, but as a post-process on an existing image rather
   than while drawing its own random beacon. This function is **seeded by
   `(scenario, base_seed, frame_index)` only** — never by detector output,
   controller output, or accepted/rejected status — so degradation cannot drift
   between Classical/AI/Hybrid runs of the same scenario/seed/frame even though
   their camera poses (and therefore their rendered frame content) diverge.
3. `apply_degradation()` never reads the true beacon position: clutter/distractor
   points are placed independently of it, exactly like `AiFrameSynthesizer`'s own
   star-clutter stage. It receives a `cv::Mat` and a seed — nothing else.

Both paths keep truth **out of** `BeaconDetector`, `AiBeaconDetector`, and
`resolve_perception()`. Truth is used only by the offline evaluator to *score*
outputs (§8, §11).

## 4. Seeds (frozen)

```
kStage4BaseSeeds = { 410101, 410102, 410103, 410104, 410105 }   (5 seeds)
```
Disjoint from the Stage-1/2 dataset seed (`26169`) and the Stage-3 parity fixture
frame. Per-scenario streams are derived with the existing, already-tested
Stage-1 utilities — no new RNG mechanism:

```
scenario_stream_seed(scenario_index, base_seed)
    = fsoc::ai::splitmix64(base_seed ^ (0x9E3779B97F4A7C15ULL * (scenario_index + 1)))

frame_seed(scenario_index, base_seed, i)
    = fsoc::ai::sample_seed_for(scenario_stream_seed(scenario_index, base_seed), i)
```

## 5. Scenario definitions (A–K, frozen)

All 11 scenarios are constructed twice — once as an `AiFrameSynthConfig` for the
common-frame benchmark, once as a `RendererConfig` + `stage4::DegradationConfig`
pair for the closed-loop benchmark. Both are given in full below.

**Common-frame baseline floor** (applied to every scenario unless overridden):
`negative_fraction=0.2`; beacon `peak_intensity=[150,255]`, `sigma_px=[1.4,2.2]`,
`anisotropy_ratio=[1,1]`, `elongation_probability=0`, `max_edge_overshoot_px=6.0`;
background `dark_offset=[2,6]`, `gradient_amplitude=[0,0]`, `vignette_strength=[0,0]`;
noise `read_sigma=[1,2]`, `shot_scale=[0,0.1]`, hot/dead/salt `=[0,0]`; optical
`weight_none=1` (all other weights 0); clutter `star_count=[0,0]`,
`bright_distractor_probability=0`, `cluster_probability=0`.

**Closed-loop floor**: `RendererConfig{beacon_peak_intensity=255,
background_intensity=5, beacon_sigma_px=2.0}` (== `baseline_runner_config()`'s
own renderer config); `DegradationConfig{read_sigma=1.5, shot_scale=0.05}`,
everything else off/`None`.

| id | name | common-frame override | closed-loop override |
|---|---|---|---|
| **A** | CLEAN | floor, unchanged | floor, unchanged |
| **B** | LOW SNR | beacon `peak_intensity=[40,90]`; noise `read_sigma=[6,10]`, `shot_scale=[0.6,1.0]` | renderer `beacon_peak_intensity=90, background_intensity=20`; degradation `read_sigma=8.0, shot_scale=0.8` |
| **C** | STAR CLUTTER | clutter `star_count=[6,9]`, `star_peak_intensity=[60,150]`, `cluster_probability=0.5` | degradation `star_count=8, star_peak=[60,150], star_sigma=[0.7,1.8]` |
| **D** | HOT PIXELS | noise `hot_pixel_count=[20,40]` | degradation `hot_pixel_count=30` |
| **E** | BRIGHT DISTRACTOR | clutter `bright_distractor_probability=1.0`, `bright_distractor_excess=[20,45]` | renderer `beacon_peak_intensity=200`; degradation distractor `peak=[230,255], sigma=[1.4,2.2]` |
| **F** | BLUR / DEFOCUS | optical `weight_none=0, weight_gaussian_blur=0.5, weight_defocus=0.5`; `gaussian_blur_sigma_px=[1.5,2.2]`, `defocus_radius_px=[2.0,3.5]` | degradation optical mode alternates `GaussianBlur`/`Defocus` by seed-index parity (even index → Gaussian σ=2.0; odd → Defocus r=3.0) — fixed at freeze time, not chosen from results |
| **G** | MOTION BLUR | optical `weight_none=0, weight_motion_blur=1.0`; `motion_blur_length_px=[6,11]`, `motion_blur_angle_rad=[0,π]` | degradation `MotionBlur`, length=9.0 px, angle=0.6 rad (fixed) |
| **H** | BACKGROUND GRADIENT | background `gradient_amplitude=[18,26]`, `vignette_strength=[0.2,0.35]` | degradation `gradient_amplitude=24.0, vignette_strength=0.3` |
| **I** | TARGET ABSENT | `negative_fraction=1.0`, `force_target=false` always; clutter `star_count=[3,7]`, `star_peak_intensity=[60,180]` | trajectory `StationaryTrajectory{Vec3{100,500,0}}` (guaranteed `OutsideFieldOfView` → renderer emits background-only every frame); degradation `star_count=4, star_peak=[60,180]` |
| **J** | MIXED RANDOMIZED | the unmodified **default** `AiFrameSynthConfig{}` (Stage-1's own training distribution), Stage-4-exclusive seeds only | degradation `read_sigma=4.0, shot_scale=0.3, hot_pixel_count=8, gradient_amplitude=12.0, vignette_strength=0.15, star_count=3, GaussianBlur σ=1.0` |
| **K** | ADVERSARIAL IDENTITY AMBIGUITY | clutter `bright_distractor_probability=1.0`, `bright_distractor_excess=[0,3]`, `star_sigma_px=[1.4,2.2]` (matches beacon size) | renderer `beacon_peak_intensity=200`; degradation distractor `peak=[195,205], sigma=[1.8,2.2]` (near-identical to the beacon) |

Scenario K is deliberately the closest to Stage-2's documented failure mode
(single-frame identity ambiguity between two similar point sources).

## 6. Frame / run counts (frozen)

**Common-frame:** 200 frames per (scenario, seed) → 1,000 per scenario → **11,000
total** samples. Each sample scored independently by Classical, AI, and Hybrid
(Hybrid reuses the same per-frame Classical + AI outputs — no extra inference).

**Closed-loop:** 8.0 s @ 50 Hz (`dt=0.02s`, unchanged) = 400 frames per
(scenario, mode, seed) → 11 × 3 × 5 = 165 runs → **66,000 steps**.

Initial conditions, identical across modes and scenarios except where the
scenario itself defines the trajectory (I only):
- `CameraConfig{}` default (HFOV 20°, VFOV 15°, ±30°/s actuator limit)
- `initial_pan_rad = initial_tilt_rad = 0`, `camera_position_m = {0,0,0}`
- trajectory: `StationaryTrajectory{Vec3{100.0, 10.0, 3.0}}` for A–H, J, K (off-
  center, inside FOV, non-trivial); `StationaryTrajectory{Vec3{100.0, 500.0, 0.0}}`
  for I (guaranteed outside FOV)
- PID / actuator limits / `dt`: `baseline_runner_config()` values, verbatim,
  unmodified

## 7. Metrics (frozen definitions)

- **accepted_rate** = accepted outputs / total frames (per mode; "accepted" =
  control-facing `BeaconDetection` present for Classical/Hybrid, thresholded
  candidate present for AI).
- **recall** (= "detection_rate", matching the Stage-2 `eval_beacon_net.py`
  convention) = TP / total positive-truth frames.
- **precision** = TP / (TP + FP) over accepted frames.
- **FPR** = FP / total negative-truth frames.
- Localization **median / MAE / RMSE / P95 / max** computed **only** over
  accepted **true-positive** frames (accepted AND truth present), Euclidean px
  error vs. truth.
- **P95**: nearest-rank, `index = ceil(0.95·N) − 1` clamped to `[0, N-1]` — the
  same method already frozen in `docs/08_TELEMETRY_SCHEMA.md`.
- **Severe-outlier counts** (frozen, §8): among accepted true-positive frames,
  counts with error `> 20 px`, `> 50 px`, `> 100 px` (cumulative, not exclusive
  buckets) plus the run's max error. All three thresholds are always reported
  together — none is chosen after seeing results.

**Hybrid-only offline safety metric** (truth used by the evaluator only, never
by `resolve_perception`): for every `DetectorDisagreement` frame where truth
says the target is present, compute the AI candidate's error against truth and
count how many exceed 20/50/100 px — "how many disagreements would have been a
bad lock had AI been trusted alone." For every `AiOnlyUnverified` frame where
truth says the target is **absent**, count it as a false-positive AI candidate
correctly withheld from control.

## 8. Closed-loop metrics (frozen)

Per (mode, scenario) aggregate: total frames, accepted-detection fraction,
target-lost frames, longest continuous loss streak, reacquisition count
(lost→tracking transitions after frame 0), mean reacquisition time in frames
(over streaks that end in reacquisition within the run), RMS / median / P95 /
max angular tracking error (magnitude `hypot(pan_rad, tilt_rad)` over frames
with a `TrackingError`), saturated-actuator frame count (`|applied_rate| >=
max_rate − 1e-9` on either axis), max commanded pan/tilt rate. Hybrid
additionally reports per-frame `PerceptionSource` counts
(`HybridAgreement/Classical/AI/None`) and `PerceptionRejectionReason` counts.

**Control-outlier safety metric** (evaluator-only, truth-based, never fed back
into control): for every frame with an *accepted* control-facing detection,
compute its pixel error against the exact truth `observation.image_point_px`
(when `Visible`) and count `> 20 / 50 / 100 px` occurrences — how often the
control loop itself was ever handed something that would have been a bad lock.

## 9. Latency (frozen methodology)

Measured **inside** the closed-loop benchmark (model already loaded and
warmed — the first 20 frames of every run are excluded as warmup): Classical =
`BeaconDetector::detect()` wall time only; AI = `AiBeaconDetector::detect()`
wall time only; Hybrid = both detector calls **plus** `resolve_perception()`
(the actual per-frame Hybrid cost, not just the AI portion). Aggregated
mean/P95 per mode across every scenario/seed. Model load time is excluded.

## 10. Artifacts (frozen layout)

```
generated/ai_stage4/
    stage4_summary.json
    stage4_summary.csv
    reports/            per-scenario markdown tables
    csv/                per-scenario, per-mode raw metric rows
    evidence/           curated PNGs + captions (§11)
```
All under the already-gitignored `generated/` tree — nothing here is committed.

## 11. Curated evidence set (frozen selection criteria)

One representative frame is saved per case, chosen by the **first** run
(scenario, seed, frame index — in that deterministic iteration order) matching
each condition below, never cherry-picked after browsing many candidates:
1. Classical correct + AI agrees (`HybridAgreement`)
2. Classical only (`Classical` source, no AI candidate)
3. AI-only candidate rejected (`AiOnlyUnverified`)
4. Classical/AI disagreement rejected (`DetectorDisagreement`)
5. A `DetectorDisagreement` frame where AI's own error vs. truth exceeds 50 px
   (a wrong-blob lock Hybrid actually prevented)
6. Target-absent frame correctly rejected (scenario I, no accepted detection)
7. A frame/scenario where Hybrid sacrifices coverage that Classical alone would
   have kept (Classical accepted, Hybrid rejected as `DetectorDisagreement` or
   `AiOnlyUnverified` — N/A for pure-Classical frames since AI is off in that
   mode; drawn from the Hybrid run directly)
8. A scenario where Hybrid provides no measurable benefit over Classical alone

## 12. What must NOT change after this point

`agreement_radius_px` (8.0), `presence_threshold` (0.95), the ONNX model, the
classical detector algorithm, PID gains, actuator limits, FOV, `dt`, and every
scenario/seed/frame-count/metric value in this document are frozen as of this
commit. If Stage-4 results are unflattering to any mode, the fix is a *future,
separately-decided* stage — not a retroactive edit here.
