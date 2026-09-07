# Model Card — TinyBeaconNet v1

**Stage 2, branch `feat/ai-perception`.** Trained + validated on synthetic data,
exported to ONNX. **NOT** integrated into the C++ closed loop (that is Stage 3).
`v1_baseline` is untouched.

## Model

| | |
|---|---|
| Name | TinyBeaconNet (depthwise-separable fully-convolutional heatmap localizer) |
| Task | single-frame FSOC beacon **presence** + sub-pixel **heatmap localization** |
| Trainable params | **27,282** (design budget < 1,000,000) |
| Ops | Conv, depthwise-separable Conv, BatchNorm, ReLU, GlobalMaxPool, Linear. No pretrained weights, no YOLO / ResNet / ViT / CLIP. |
| Design decisions | `DECISIONS.md` ADR-015, ADR-016, **ADR-017** |

## Input contract (frozen — `tools/ai/common.py`)

- Source frame: `CV_8UC1` grayscale, **640 × 480** (native virtual camera).
- Preprocess: `cv2.resize(frame, (320, 240), INTER_AREA)` → `float32` → `× 1/255`
  → NCHW tensor **`[1, 1, 240, 320]`**. No mean/std standardization.
- OpenCV 5.0 `INTER_AREA` for the exact ÷2 downscale = 2×2 box average; the C++
  OpenCV-DNN path (Stage 3) calls the identical routine (parity tol ±1 count).

## Output contract (frozen)

- `presence_logit` `[N, 1]` — raw logit; `sigmoid()` applied **outside** the graph.
- `heatmap_logit` `[N, 1, 60, 80]` — raw logits; `sigmoid()` outside the graph.
- Decode: sigmoid → integer argmax → 5×5 soft-argmax (intensity-weighted) →
  `(x_hm, y_hm)` → `x_orig = (x_hm + 0.5)·8 − 0.5` (same for y). Origin top-left,
  `+x` right, `+y` down. `peak_confidence` = sigmoid at the argmax cell.

## Training data

- **Synthetic only.** `generate_ai_dataset` (C++ `fsoc_ai_datagen`), **seed 26169**.
- **train 6000 / val 1200 / test 1200**; 75 % positive / 25 % negative per split.
- Contiguous disjoint index blocks; per-sample seed `sample_seed_for(26169, i)`.
  Integrity verified: **8400 / 8400 unique image hashes**, no cross-split leakage,
  no sentinel labels, class balance exact, positive centroids uniform over the
  frame (central-quarter-box occupancy 0.245 vs 0.25 uniform).
- Domain randomization (full ranges: `docs/20 §3`): beacon peak 70–255, PSF σ
  1.2–3.2 px, mild anisotropy, edge overshoot ≤ 6 px; background dark offset +
  gradient + vignette; Gaussian read noise σ 1–6, shot-like noise; hot / dead /
  salt pixels; one of none / Gaussian-blur / defocus / motion-blur; 0–9 star
  distractors, a **brighter-than-beacon** distractor w.p. 0.30, tight clusters
  w.p. 0.20.

## Training

- Device **MPS (Apple)**. Seed 26169 (`random`, `numpy`, `torch`, DataLoader gen).
- **Dataset determinism is exact** (byte-identical frames, proven by regen).
  **MPS training is seeded but not bit-identical** (conv/matmul reduction order).
- Optimizer **AdamW**, lr 2e-3, weight_decay 1e-4, **cosine anneal**, batch 32,
  40 epochs / **early-stopped at 21**, patience 12.
- Loss `L = BCEWithLogits(presence) + weightedMSE(sigmoid(heatmap), gaussian)`,
  foreground weight `1 + 80·[t>0]` (**ADR-017**; plain MSE stalls — the 60×80
  grid is ~97 % background).
- Checkpoint criterion: min validation `val_total` (= the training objective).
  **Best epoch: 9.**

## Frozen presence threshold

**0.95** — calibrated on the **validation** split only (`eval_beacon_net.py`),
rule "lowest val threshold with FPR ≤ 0.02; none achieved it, so the rule's
fallback — lowest-FP threshold in the 0.05–0.95 sweep — is frozen". Frozen
**before** the test split was scored. val @ 0.95: P/R/F1 = 0.974 / 0.376 / 0.542,
FPR 0.030.

## Test metrics (frozen checkpoint + frozen threshold, untouched test split, n = 1200)

**Presence @ 0.95:** precision **0.989**, recall **0.404**, FPR **0.013**,
neg-reject **0.987**. Confusion TP 364 / TN 296 / FP 4 / FN 536.

**Localization** — Euclidean px between decoded centroid and the true sub-pixel
label, over frames that are **both** ground-truth-positive **and** predicted
present at the frozen threshold (n = 364):

| median | p90 | p95 | MAE | RMSE | max | ≤ 2 px | ≤ 5 px | ≤ 10 px |
|---|---|---|---|---|---|---|---|---|
| **1.71** | 2.91 | 5.66 | 8.70 | 52.9 | 631 | 64.6 % | 94.0 % | **97.5 %** |

(RMSE / max are inflated by ~2 % wrong-blob detections that survive the threshold.)

**Degradation breakdown (committed detections, test positives):**

| stratum | n | detect rate | loc median px | ≤ 10 px |
|---|---|---|---|---|
| clean (peak ≥ 150, no optical, ≤ 2 stars, no distractor) | 40 | 0.58 | 1.29 | 100 % |
| star clutter (≥ 6 stars) | 356 | 0.40 | 1.76 | 97.9 % |
| bright distractor (brighter than beacon) | 265 | 0.40 | 1.76 | 97.2 % |
| hot pixels (≥ 6) | 488 | 0.39 | 1.62 | 98.4 % |
| Gaussian blur | 216 | 0.36 | 1.62 | 96.1 % |
| defocus | 140 | 0.42 | 1.84 | 98.3 % |
| motion blur | 125 | 0.44 | 1.50 | 98.2 % |
| background gradient (amp > 18) | 278 | 0.38 | 1.60 | 98.1 % |
| **low SNR (peak/read σ < 20)** | 73 | **0.12** | 2.47 | 88.9 % |
| **dim beacon (peak < 100)** | 151 | **0.12** | 2.20 | 88.9 % |
| **edge-clipped** | 40 | **0.03** | 10.9 | 0 % |
| **target absent** | 300 | false-alarm 0.013 | — | neg-reject 0.987 |

**Python (torch, CPU single-thread) latency:** mean 3.4 ms, p95 3.8 ms (~300 fps).
This is **not** the deployed number — C++ OpenCV-DNN latency is a Stage-3
measurement (`fsoc_ai_validation`).

## ONNX

| | |
|---|---|
| File | `models/tiny_beacon_net.onnx`, **109.8 KiB**, opset **12**, legacy exporter (`dynamo=False`) |
| Input | `input` `[1, 1, 240, 320]` float32, dynamic batch axis |
| Outputs | `presence_logit` `[1, 1]`, `heatmap_logit` `[1, 1, 60, 80]`, dynamic batch |
| `onnx.checker` | **OK** |
| Graph ops | Conv, Gemm, MaxPool, Relu, Flatten (BatchNorm folded into Conv) |
| PyTorch ↔ ONNX Runtime parity (12 fixed real frames) | presence logit max\|Δ\| **9.5e-7**, heatmap **7.6e-6**, decoded centroid **1.8e-6 px** — tolerance 2e-3 / 3e-3 / 0.05 px |

## Known failure modes

1. **Recall ≈ 40 % at the safe threshold.** The presence head cannot separate
   "beacon present" from "clutter only" (val ROC-AUC ≈ 0.82). The model
   **abstains** on low-SNR, dim (peak < 100) and edge-clipped beacons rather than
   lock onto the wrong blob — safe, but it will miss those beacons. Ten training
   variants (loss form, width 16–32, stem stride, a global-context head) all
   plateau here; a single-frame ~27 k-param CNN cannot reliably out-select a
   distractor deliberately rendered brighter than the beacon with an overlapping
   σ range (30 % of positives, by dataset design). Recall is expected from the
   Stage-3 hybrid policy (classical detector) and the Phase-2 spatio-temporal
   detector (`docs/19 §8`).
2. **~2 % confident wrong-blob detections** survive the threshold (RMSE 53 px,
   max 631 px on committed detections). `peak_confidence` does **not** separate
   these from correct ones (correct mean 0.972, wrong mean 0.965) — the Stage-3
   hybrid classical-agreement check is the intended guard.
3. **Soft-argmax decoder** has a ~1–2 px inward bias when the true centre sits
   near a heatmap half-cell boundary (stride 8) — measured in the Stage-2
   heatmap-decode audit. Small vs the coarse-alignment budget (20° × 15° FOV over
   640 × 480 → 0.031 °/px); documented for Stage-3 review.

## Intended use

Stage 3: swappable `AiBeaconDetector` behind the frozen
`std::optional<BeaconDetection>` contract; a config-driven hybrid policy
arbitrates classical vs AI (`docs/19 §5`). Diagnostics (`confidence`,
`peak_confidence`, `inference_ms`) are telemetry-only and never reach the PID.

## NOT claimed

- No real optical-sensor data, no measured atmospheric turbulence, no flight
  heritage, no hardware-in-the-loop.
- Not production-ready, not hardware-qualified, not atmospherically validated.
- No C++ / OpenCV-DNN inference-latency benchmark yet (Stage 3).
- No "AI beats the classical detector" claim — that comparison is Stage 4.
- The legitimate Stage-2 claim: *a lightweight neural beacon-localization model
  was trained and validated on a deterministic domain-randomized synthetic
  optical dataset and exported to a verified ONNX representation; at a
  validation-calibrated safe threshold it localizes the beacons it commits to at
  ~1.7 px median with a ~1 % false-lock rate, at the cost of ~40 % recall.*
