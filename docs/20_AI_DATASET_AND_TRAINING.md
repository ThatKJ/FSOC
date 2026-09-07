# 20 — AI Dataset & Training (V2)

**Status:** phase in progress on `feat/ai-perception`. Dataset generation and the
training toolchain are complete; training results are filled in after a run.
See `docs/19_AI_PERCEPTION_ARCHITECTURE.md` for the model and `docs/21_AI_VALIDATION.md`
for evaluation.

## 1. Data source — our own virtual camera, not a downloaded set

The task is highly specialised (a narrow-FOV virtual FSOC tracking camera looking
at a point beacon), so training data is **synthesised from the same virtual
camera model** used by the closed loop. `fsoc_ai_datagen`
(`include/fsoc/ai_frame_synth.hpp`, `src/ai_frame_synth.cpp`) starts from the
identical analytic Gaussian beacon that `SyntheticCameraRenderer` (Step 4, frozen)
produces, then layers difficult optical conditions on top. It is **dataset
tooling only** — not in the control path, and it does not modify the frozen
renderer.

Generator: `apps/generate_ai_dataset.cpp` → executable `generate_ai_dataset`.

```
generated/ai_dataset/                       (git-ignored — regenerate from seed)
├── train/  train_000000.png ...            CV_8UC1 640×480, lossless PNG
├── val/    val_000000.png ...
├── test/   test_000000.png ...
└── metadata/
    ├── train.jsonl  val.jsonl  test.jsonl  one label record per line
    └── dataset.json                        generator version, seed, ranges,
                                            split index ranges, pos/neg counts
```

## 2. Frozen operation order (one sample)

Every sample is built in exactly these stages (negatives skip stage 3 only):

1. float working buffer filled with `background.dark_offset`
2. `+` linear background-gradient plane, `×` radial vignette
3. **positive only:** `+` analytic Gaussian beacon at sub-pixel `(x, y)`, drawn
   peak, drawn σ, optional mild anisotropy (√ratio major / minor) + rotation; the
   centre may sit up to `max_edge_overshoot_px` (6) outside the frame → partial
   edge clipping
4. `+` `K` star-like clutter spots; with probability `bright_distractor_probability`
   one is forced **brighter than the beacon** (`beacon_peak + excess`); optional
   tight 2–4 spot cluster
5. exactly one optical-degradation operator, chosen by weight:
   `none | Gaussian blur | defocus disk | linear motion blur`
6. shot-like noise (Gaussian approximation to Poisson, variance ∝ signal) then
   additive Gaussian read noise
7. hot pixels (→ near-max), dead pixels (→ 0), salt impulses (→ max)
8. clamp `[0, 255]`, round to `CV_8UC1`

All randomness is drawn from `std::mt19937_64` seeded per sample — **never**
`cv::randn`/`cv::randu` (not portably reproducible). `synthesize(seed)` is a pure
function: equal seed ⇒ byte-identical frame and labels on any platform (unit test
`fsoc_ai_datagen_tests`).

## 3. Domain randomization ranges (generator v1.0.0 defaults)

| group | parameter | range |
|---|---|---|
| beacon | peak intensity (counts) | 70 – 255 |
| | PSF σ (px) | 1.2 – 3.2 |
| | anisotropy (major/minor) | 1.0 – 1.6, applied w.p. 0.25 |
| | max edge overshoot (px) | 6 |
| background | dark offset (counts) | 2 – 14 |
| | gradient amplitude (counts) | 0 – 26 |
| | vignette strength | 0 – 0.35 |
| noise | read σ (counts) | 1 – 6 |
| | shot scale | 0 – 1 (× Poisson variance) |
| | hot / dead / salt pixels | 0–12 / 0–6 / 0–20 |
| optical | mode weights | none .45 / gauss .25 / defocus .15 / motion .15 |
| | Gaussian blur σ (px) | 0.6 – 2.2 |
| | defocus radius (px) | 1.0 – 3.5 |
| | motion blur length (px) | 3 – 11, any angle |
| clutter | star count | 0 – 9 |
| | star peak (counts) | 40 – 200 |
| | star σ (px) | 0.7 – 1.8 |
| | bright distractor | w.p. 0.30, excess 6 – 45 counts over beacon |
| | tight cluster | w.p. 0.20 |

The full realised parameter set for every frame is written to the `.jsonl`
manifest, so any sample can be explained, regenerated, or stratified. A single
scalar `difficulty ∈ [0,1]` proxy (weighted mix of inverse-SNR, read noise,
optical strength, clutter count, bright-distractor flag) is recorded **for
stratified reporting only** — it is never a label and the model never sees it.

## 4. Negatives (mandatory)

`negative_fraction` default **0.25** (≈ 25–30 % as the brief suggests). A negative
frame runs every stage except the beacon: background + gradient + vignette +
clutter + optical + noise + hot/dead/salt, `target_present = false`, `x_px` /
`y_px` = `null` (no sentinel). The learned detector must return **no target** →
`std::nullopt` through the perception contract for these.

## 5. Determinism & split integrity

- **Dataset seed:** `26169`.
- One global sample-index space `[0, N)`. Splits are **contiguous, disjoint
  blocks**: `train = [0, n_train)`, `val = [n_train, n_train+n_val)`,
  `test = [..., N)`. Recorded in `metadata/dataset.json → splits.*.global_index_range`.
- Per-sample seed `sample_seed_for(26169, global_index) = splitmix64(26169 ⊕
  splitmix64(global_index + k))`. A sample is therefore fully addressable and
  **no frame can appear in two splits** — different index ⇒ different seed ⇒
  different frame. `fsoc_ai_datagen_tests` checks 20 000 seeds are collision-free.
- Negatives are evenly spaced inside each split (Bresenham pick), giving an exact
  count `round(split_size × negative_fraction)` with no positional clustering.
- `train_beacon_net.py` and `eval_beacon_net.py` assert train/val and val/test id
  sets are disjoint at startup and abort on any overlap.
- Test degradation combinations are drawn from the same distribution but from
  disjoint seeds, so they are not duplicates of training frames.

## 6. Labels

Positive: `target_present = true`, `x_px`, `y_px` (sub-pixel, original 640×480).
Training builds the target heatmap on the fly — a `[60, 80]` Gaussian, σ = 1.75
cells, peak 1.0, at `orig_to_heatmap(x, y)` (truncated by the border for clipped
beacons). Negative: `target_present = false`, all-zero heatmap. **Truth labels
are used only in dataset generation, training, and evaluation — never at
inference.**

## 7. Loss

```
L = λ_p · BCEWithLogits(presence_logit, target_present)
  + λ_h · weightedMSE( sigmoid(heatmap_logit), gaussian_heatmap )

weightedMSE(p, t) = Σ w·(p−t)²  /  Σ w ,   w = 1 + pos_weight·[t > 0]
```

`λ_p = λ_h = 1.0`; `pos_weight = 80` (all CLI-tunable — `--lambda-presence`,
`--lambda-heatmap`, `--heatmap-pos-weight` — and logged separately every epoch).

**Why weighted, not plain MSE (ADR-017).** The 60×80 target grid is ~97 %
background (the σ = 1.75-cell Gaussian covers ~120 of 4800 cells). *Un*weighted
MSE on the sigmoid surface is minimised by a near-flat ~0 map (MSE ≈ 1.4e-3) and
the few foreground cells cannot pull it into a peak — the first real training run
stalled at centroid MAE ≈ 40 px with this loss. Up-weighting the `t > 0` cells by
`1 + pos_weight` makes the loss actually shape the peak the soft-argmax decoder
integrates (probe: MAE 42 → 12–15 px). Focal / penalty-reduced BCE on the soft
heatmap was tried and rejected — it destabilised the presence head. `pos_weight
= 0` recovers the original plain MSE.

The heatmap loss is applied to negatives too (all-zero target → every cell keeps
weight 1) so the surface goes flat when no beacon is present.

**Checkpoint criterion:** `best.pt` = the epoch with the lowest
`val_total = λ_p · BCE(presence) + λ_h · weightedMSE(heatmap)` on the **validation**
split — i.e. the training objective, evaluated on held-out data. The test split is
never consulted for checkpoint selection.

## 8. Training toolchain (`tools/ai/`)

Offline only (Python permitted for this per AI brief §5; deployed detector is
C++/OpenCV-DNN). Setup and exact reproduce commands: `tools/ai/README.md`.

| script | role |
|---|---|
| `common.py` | frozen preprocessing / heatmap / decode / coord maps — mirrored in C++ |
| `model.py` | `TinyBeaconNet` |
| `dataset.py` | reads `generated/ai_dataset/` → tensors |
| `train_beacon_net.py` | AdamW + cosine LR, per-epoch val metrics, `best.pt` / `last.pt`, early stopping (`--patience`), `training_history.json` (ADR-017 weighted heatmap loss + `--heatmap-pos-weight`) |
| `eval_beacon_net.py` | threshold sweep on **val**, freeze threshold, stratified **test** metrics, latency |
| `export_onnx.py` | `best.pt` → `models/tiny_beacon_net.onnx` (opset 12, dynamic batch, `dynamo=False`) + `.meta.json`; torch↔onnxruntime parity check |
| `make_parity_fixture.py` | writes `tests/fixtures/ai_parity/` for the C++ parity tests |
| `selfcheck.py` | fast learn-ability regression (guards ADR-017 loss/pool) |

Device auto-selects Apple MPS → CUDA → CPU. Seeds: `random`, `numpy`, `torch`,
DataLoader generator all set from `--seed` (default 26169).

## 9. Training configuration (recorded after the run)

```
dataset            : generated/ai_dataset  (train 6000 / val 1200 / test 1200, neg 0.25)
seed               : 26169
epochs             : 40 (early-stop patience 8)
batch size         : 32
optimizer          : AdamW  lr 2e-3  weight_decay 1e-4  cosine anneal
lambda presence/hm : 1.0 / 1.0
heatmap pos_weight : 80        (ADR-017 foreground-weighted MSE)
presence pooling   : global max (ADR-017)
device             : MPS (Apple) — dataset determinism is exact; MPS training is
                     seeded but not bit-identical (conv/matmul reduction order)
trainable params   : 27,282   (budget < 1,000,000)
```

## 10. Training results (Stage-2 run, seed 26169, MPS, 2026-09-04)

| metric (best-val checkpoint) | value |
|---|---|
| epochs run / best val epoch | 21 (early stop) / **9** |
| trainable params | 27,282 |
| val presence precision / recall / F1 (thr 0.5) | 0.825 / 0.923 / 0.872 |
| val presence false-positive rate (thr 0.5) | 0.587 |
| val centroid MAE / RMSE (px, thr 0.5, all val TPs) | 45.1 / 158.5 |
| **frozen presence threshold** | **0.95** — rule: lowest val threshold with FPR ≤ 0.02; none achieved it (presence-head limit), so the rule's fallback (lowest-FP threshold in the 0.05–0.95 sweep) is frozen. val P/R/F1 @ 0.95 = 0.974 / 0.376 / 0.542, FPR 0.030 |
| **test @ frozen 0.95** — presence | precision **0.989**, recall **0.404**, FPR **0.013**, neg-reject **0.987** (TP 364 / TN 296 / FP 4 / FN 536) |
| **test @ frozen 0.95** — localization (committed detections, n=364, Euclidean px) | median **1.71**, p90 **2.91**, p95 **5.66**, MAE 8.70, RMSE 52.9, max 631; **97.5 % ≤ 10 px**, 94.0 % ≤ 5 px |
| python (torch CPU) latency mean / p95 | 3.4 ms / 3.8 ms (~300 fps, single frame) |
| torch ↔ ONNX Runtime parity (12 fixed frames) | presence logit max\|Δ\| 9.5e-7, heatmap 7.6e-6, decoded centroid 1.8e-6 px |

### Known limitation (Stage-2 finding — see ADR-017 coda)

The **presence head** does not separate "beacon present" from "clutter only" well
enough to give both low false-positive and high recall (val AUC ≈ 0.82). At the
frozen safe threshold the model is a **precise but low-recall** detector: of the
~40 % of beacons it commits to, **97.5 % localize within 10 px** (median 1.7 px)
and the false-lock rate is ~1 %. It **abstains** on the cases it cannot trust —
detection rate drops to ~12 % on low-SNR (peak/read σ < 20) and dim (peak < 100)
beacons and ~2 % on edge-clipped beacons — rather than locking onto the wrong
blob. When it *does* commit under clutter / a brighter distractor / blur, median
error stays ≈ 1.7 px (97 % ≤ 10 px), so the learned PSF signature is real; the
gap is recall, not precision. Ten training variants (loss form, channel width
16–32, stem stride, a global-context head) all plateau here — a ~27 k-param
single-frame CNN cannot reliably out-select a distractor deliberately rendered
brighter than the target with an overlapping σ range (30 % of positives, by
design). Recall is expected to come from the **Stage-3 hybrid policy** (the
classical detector) and, longer term, the **Phase-2 spatio-temporal detector**
(`docs/19 §8`).

Deployed C++ OpenCV-DNN latency (mean / median / p95 / max / throughput) is
measured by `fsoc_ai_validation` and reported in `docs/21_AI_VALIDATION.md`.
