# 19 — AI Perception Architecture (V2)

**Status:** phase in progress on `feat/ai-perception`. `v1_baseline` is frozen and
untouched. This document is the design contract for the learned perception stack;
see `DECISIONS.md` ADR-015 / ADR-016 / ADR-017 / **ADR-018** and
`docs/15_INTERFACE_CONTRACTS.md`. §5 below is the Safe Hybrid policy frozen by
ADR-018 after Stage-2 training evidence — documentation only, Stage 3 not started.

## 1. Why AI, and where it lives

The validated v1 detector is:

```
pixels → threshold → 8-connected components → brightest component
       → intensity-weighted centroid → BeaconDetection
```

Transparent, deterministic, ~0.02 px sub-pixel error on clean interior frames.
Its weakness is **selection under difficulty**: it has no model of *which* bright
region is the beacon. When the frame degrades — low SNR, star-like clutter,
multiple bright points, a distractor brighter than the target, hot pixels, blur,
defocus, motion blur, background gradient, partial edge clipping, or the beacon
briefly absent — "brightest integrated signal" is not the right answer, and the
classical detector either locks onto the wrong blob or returns a centroid for a
target that is not there.

`TinyBeaconNet` is a small CNN trained specifically on those conditions. It
**only changes perception**. The learned detector emits the *same*
`std::optional<BeaconDetection>` as the classical one; everything downstream —
`compute_tracking_error`, the PID law and its frozen gains, `PanTiltCamera`, the
actuator limits, the `SimulationRunner` step order, the Step-10 gates — is
unchanged.

```
                          CAMERA FRAME  (CV_8UC1 640×480)
                                 │
                  ┌──────────────┴───────────────┐
                  │                              │
          CLASSICAL DETECTOR             AI BEACON DETECTOR
          (fsoc_perception, v1)          (fsoc_ai_perception, TinyBeaconNet)
          brightest CC → weighted        preprocess → ONNX → (presence, heatmap)
          sub-pixel centroid             → soft-argmax sub-pixel centroid
                  │                              │
                  └──────────────┬───────────────┘
                                 │
                        HYBRID PERCEPTION  (config-driven policy)
                                 │
                        std::optional<BeaconDetection>     ← unchanged contract
                                 │
                          compute_tracking_error           ← unchanged
                                 │
                          PIDController (kp=12, ki=0, kd=0) ← unchanged
                                 │
                          PanTiltCamera::step              ← unchanged
```

**AI EXISTS ONLY IN PERCEPTION.** The gimbal control law is the validated v1 PID.

## 2. TinyBeaconNet

Fully-convolutional heatmap-localization network. **Not** YOLO / bounding-box
detection — the target is a sub-pixel point source, so the network predicts a
location *heatmap* plus a *presence* probability, and a sub-pixel decoder reads
the centroid off the heatmap.

```
Input  [N, 1, 240, 320]  float32 ∈ [0,1]
  stem      Conv 1→16  3×3 s2  · BN · ReLU        → 16 ×120×160
  block1    DWConv 16 s2 · PWConv 16→32 · BN·ReLU → 32 × 60× 80
  block2    DWConv 32 s1 · PWConv 32→48 · BN·ReLU → 48 × 60× 80
  block3    DWConv 48 s1 · PWConv 48→64 · BN·ReLU → 64 × 60× 80   (= HM_H×HM_W)
  ├─ heatmap head   Conv 64→32 3×3 · BN·ReLU · Conv 32→1 1×1 → [N,1,60,80] logits
  └─ presence head  GlobalMaxPool(64) · FC 64→32 · ReLU · FC 32→1 → [N,1] logit
```

- **Presence pooling is global *max*, not average** (ADR-017): the beacon occupies
  << 1 cell of the 60×80 trunk map, so averaging over ~4800 cells destroys the
  "is there a peak anywhere" signal. `MaxPool` stays inside the frozen opset-12
  ONNX interface and is OpenCV-DNN-safe.
- **Parameter budget:** < 1,000,000 trainable params. Actual **27,282** (printed by
  `train_beacon_net.py`; recorded in `models/tiny_beacon_net.meta.json` and the
  model card).
- **Ops only:** Conv, depthwise-separable Conv, BatchNorm, ReLU, global max
  pool, Linear. No pretrained weights, no ResNet/ViT/CLIP.
- **Raw logits out.** `sigmoid()` is applied *outside* the ONNX graph, identically
  in Python eval and in C++, so the graph stays tiny and `BCEWithLogits` is exact.

## 3. Frozen numeric contract (`tools/ai/common.py` ⇔ `src/ai_beacon_detector.cpp`)

| constant | value | meaning |
|---|---|---|
| `ORIG_W × ORIG_H` | 640 × 480 | native virtual-camera frame |
| `IN_W × IN_H` | 320 × 240 | network input (native ÷ 2) |
| `HM_W × HM_H` | 80 × 60 | heatmap head (native ÷ 8, input ÷ 4) |
| `INPUT_STRIDE` | 8 | original px per heatmap cell |
| `HEATMAP_SIGMA_CELLS` | 1.75 | Gaussian label σ, in heatmap cells |
| `DECODE_WINDOW_RADIUS` | 2 | soft-argmax uses a 5×5 window around the peak |

**Preprocessing (frozen):** `cv2.resize(frame, (320,240), INTER_AREA)` →
`float32` → `× 1/255` → NCHW `[1,1,240,320]`. No mean/std standardization. The C++
path calls the identical OpenCV `INTER_AREA` resize, so Python↔C++ preprocessing
is numerically equivalent (parity test, tolerance ±1 count).

**Coordinate map (pixel-centre aligned, matches renderer `cx = W/2.0`):**
```
x_hm   = (x_orig + 0.5) / 8 − 0.5
x_orig = (x_hm   + 0.5) × 8 − 0.5           (same for y)
```
Convention unchanged: origin top-left, `+x` right, `+y` down.

**Heatmap decode (frozen):**
1. `sigmoid` the heatmap logits.
2. integer `argmax` → peak cell.
3. soft-argmax = intensity-weighted centroid over the 5×5 window (clamped to the
   grid) using the sigmoid values as weights → sub-pixel `(x_hm, y_hm)`.
4. map back to original pixels.
`peak_confidence` = sigmoid value at the argmax cell.

## 4. C++ AI detector (`fsoc_ai_perception`, lands in a later stage)

```cpp
struct AiBeaconDetectorConfig {
    std::string model_path;        // models/tiny_beacon_net.onnx
    double presence_threshold;     // from models/threshold.json (val-calibrated)
    int input_width  = 320;
    int input_height = 240;
};

struct AiBeaconDetection {
    BeaconDetection detection;     // the controller-facing contract
    double confidence;             // sigmoid(presence_logit) ∈ [0,1] — DIAGNOSTIC
    double peak_confidence;        // heatmap peak sigmoid — DIAGNOSTIC
    double inference_ms;           // DIAGNOSTIC
};

class AiBeaconDetector {
public:
    explicit AiBeaconDetector(AiBeaconDetectorConfig);   // validates the model file
    [[nodiscard]] std::optional<AiBeaconDetection> detect(const cv::Mat& frame) const;
};
```

- Constructor fails cleanly (`std::invalid_argument` / `std::runtime_error`) on:
  missing / unreadable model, output tensor shapes ≠ the frozen contract, invalid
  config, invalid dimensions.
- `detect()` rejects an empty frame or any type other than `CV_8UC1`, matching the
  classical detector's validation style.
- The controller path consumes only `std::optional<BeaconDetection>`. `confidence`
  is exposed for telemetry/hybrid logic and never reaches the PID.

## 5. Perception modes & hybrid policy — Safe Hybrid (ADR-018, post-Stage-2)

```cpp
enum class PerceptionMode   { Classical, AI, Hybrid };            // control-relevant selection
enum class PerceptionSource { None, Classical, AI, HybridAgreement };  // DIAGNOSTIC only
enum class PerceptionRejectionReason {                            // DIAGNOSTIC only —
    NotApplicable, AiOnlyUnverified, DetectorDisagreement          // meaningful iff result == nullopt
};
```

`SimulationRunnerConfig` gains a `PerceptionMode` field, **default `Classical`**
(bit-identical to v1 — regression-tested). `PerceptionMode::Hybrid` is the **Safe
Hybrid** policy below. It replaces the ADR-016 draft's confidence-override escape
hatch, which Stage-2 training evidence invalidated (ADR-018 §"Stage-2 evidence").
All thresholds config-driven.

| case | situation | control-facing result | `PerceptionSource` | rejection reason |
|---|---|---|---|---|
| 1 | classical **and** AI detect, centroids within `agreement_radius_px` (**8.0 px**, frozen — see below; not an ML threshold) | **classical** centroid (best clean sub-pixel precision) | `HybridAgreement` | — |
| 2 | classical only | classical centroid | `Classical` | — |
| 3 | **AI only** | `std::nullopt` — **no control authority.** AI candidate (centroid, confidence) retained as diagnostics only | `None` | `AiOnlyUnverified` |
| 4 | classical **and** AI disagree (> `agreement_radius_px`) | `std::nullopt` — **unconditional reject**, no confidence override | `None` | `DetectorDisagreement` |
| 5 | neither detects | `std::nullopt` | `None` | `NotApplicable` |

Two unrelated detections are **never averaged**; the brightest is never preferred;
AI confidence is **never** an override path for cases 3/4 (retired by ADR-018). On
`std::nullopt` the existing target-loss policy runs unchanged (PID reset, zero
command, camera holds).

**Why cases 3 and 4 both reject unconditionally.** Stage-2 test evidence: peak
confidence does not separate correct (mean ≈ 0.972) from wrong (mean ≈ 0.965)
*accepted* detections, and one accepted detection at confidence ≈ 0.999 has a
≈ 630.99 px centroid error (`FAILURE_wrong_blob__test_000821.png`). No confidence
cut safely resolves either "AI alone" or "AI disagrees with classical" — a
momentary `TargetLost` is strictly safer than commanding toward a wrong optical
source. Full evidence and rationale: `DECISIONS.md` ADR-018.

**Agreement radius (frozen).** `agreement_radius_px = 8.0` px — the source-space
size of one heatmap cell (`INPUT_STRIDE = 8`, §3 above; 640/80 = 480/60 = 8), sized
to absorb ordinary heatmap-grid quantization and the model's own accepted-detection
error (median 1.71 px, P95 5.66 px test) without letting a large disagreement
through. **Not** an ML confidence threshold; **not** to be tuned against future
Stage-4 closed-loop results.

**`PerceptionMode::AI` is a different thing.** That explicit, non-default mode
exists for diagnostics and controlled comparison — there the thresholded AI
candidate *is* exposed as the detector output, because the point of that mode is
to characterise the network, not to drive the gimbal. This does **not** relax case
3 above: `PerceptionMode::Hybrid` never grants an AI-only detection control
authority, and `PerceptionSource::AI` is never emitted under Hybrid mode. Default
production/demo perception remains `Classical` (ADR-016) until further validation
changes that.

## 6. Diagnostics (telemetry only — never control)

`ai_confidence`, `perception_source`, `perception_rejection_reason` (ADR-018 —
meaningful iff the control-facing result is `std::nullopt` under
`PerceptionMode::Hybrid`), `classical_ai_distance_px`, `ai_inference_ms`. These are
additive telemetry fields; they do not touch `TrackingState` / `DemoRunState` and
the PID never reads them.

## 7. Synthetic-to-real limitation

`TinyBeaconNet` v1 is trained **exclusively** on domain-randomized synthetic
virtual-camera imagery from `fsoc_ai_datagen`. No real optical sensor data, no
measured atmospheric turbulence, no flight heritage, no hardware-in-the-loop. The
domain randomization (see `docs/20`) is broad, but a real sensor will have noise
statistics, PSF shape, and fixed-pattern artefacts this model has not seen. This
is stated in `models/MODEL_CARD.md` and must not be overstated in the demo.

## 8. Documented future work (NOT this phase)

Phase 2 spatio-temporal (3-frame) detector · Phase 3 UKF state estimation ·
Phase 4 motion prediction / feed-forward · Phase 5 MPC · Phase 6 satellite
ephemeris + SGP4 coarse pointing · Phase 7 real camera + physical gimbal.

## 9. Future temporal reacquisition gate (ADR-018 → implemented by ADR-019, AI-only case still deferred)

**Update (ADR-019, P0-v2):** the prerequisite this section named — a runtime
motion-consistency gate, decided from observation history only — now EXISTS and is
tested: `fsoc::TargetTracker` (`include/fsoc/target_tracker.hpp`), an alpha-beta
filter with an `outlier_gate_px` temporal-consistency check, additive and default-off.
It is layered AFTER `resolve_perception()` (gating whatever Classical/Hybrid already
produced), not inside it — so the specific reacquisition path described below (letting
an AI-only candidate through `resolve_perception()` itself) remains unimplemented; see
ADR-019 and `docs/MVP_ABLATION.md` for what was measured and what is still open.

Safe Hybrid case 3 (§5) gives AI-only detections no control authority because a
single frame cannot confirm target identity. That may be safely reconsidered now that
a runtime motion-consistency gate exists — this is the Phase-2 spatio-temporal
detector (§8) made concrete for the AI-only case specifically:

```
previous accepted track
        +
current AI candidate
        ↓
temporal consistency  (motion-consistency gate)
        ↓
trusted reacquisition candidate
```

**Inputs:** the previously **accepted** control-facing centroid / track history and
recent target velocity, both derived from **runtime observation only**. The gate
**MUST NOT** use `TargetState` truth, the exact simulated `Projection`, trajectory
truth, future target position, or any diagnostic truth-error field — the ADR-004
ground-truth boundary (`docs/15 §"Layers stay distinct"`) applies to this gate
exactly as it does to the classical and AI detectors themselves.

This is the proper mechanism for resolving single-frame target-identity ambiguity —
**not** a wider `agreement_radius_px`, **not** a confidence override on cases 3/4.
The gate itself is implemented (ADR-019); letting it unlock AI-only reacquisition
through `resolve_perception()` specifically is not — no interface frozen here beyond
the constraint above.
