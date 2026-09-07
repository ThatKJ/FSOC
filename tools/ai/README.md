# `tools/ai/` — TinyBeaconNet offline training toolchain

> **Offline only.** Nothing in this directory runs in the C++ closed-loop or the
> demo. Python is permitted here strictly for dataset tooling and ML training
> (AI PERCEPTION V2 brief, §5). The deployed detector is C++ + OpenCV-DNN.

The learned detector solves a real weakness of the classical baseline: on clean
Gaussian beacons the classical "brightest connected component → weighted
centroid" pipeline is excellent, but it has no notion of *which* bright blob is
the beacon. Under low SNR, star clutter, hot pixels, blur/defocus, background
gradients, or a distractor brighter than the target, it locks onto the wrong
thing or reports a target that is not there. `TinyBeaconNet` is trained to
localise the beacon (or report its absence) under exactly those conditions.

## Pipeline

```
C++ virtual camera  ──►  generate_ai_dataset  ──►  generated/ai_dataset/{train,val,test}
                                                        │
                                    tools/ai/train_beacon_net.py  (PyTorch)
                                                        │
                                              models/checkpoints/best.pt
                                                        │
                        tools/ai/eval_beacon_net.py   (calibrate threshold on VAL,
                                                        score TEST, latency)
                                                        │
                          tools/ai/export_onnx.py  ──►  models/tiny_beacon_net.onnx
                                                        │
                       tools/ai/make_parity_fixture.py ─► tests/fixtures/ai_parity/
                                                        │
                                        C++ AiBeaconDetector (OpenCV DNN)
```

## One-time setup

```bash
python3.12 -m venv tools/ai/.venv
source tools/ai/.venv/bin/activate
pip install -r tools/ai/requirements.txt
```

## Reproduce the model from scratch

```bash
# 1. build the C++ tools (OpenCV required). Use the RELEASE build for dataset
#    generation — it is ~5x faster and the output is bit-identical.
cmake --preset release && cmake --build --preset release --target generate_ai_dataset

# 2. generate the deterministic dataset (seed 26169; ~8.4k frames ≈ 60s release,
#    ≈ 1.3 GB under generated/ — git-ignored, regenerate any time)
./build/release/generate_ai_dataset --out generated/ai_dataset \
    --train 6000 --val 1200 --test 1200 --neg-frac 0.25 --seed 26169

# (rebuild the debug tree for the C++ test suite / AI validation later)
cmake --preset debug && cmake --build --preset debug

# 3. train  (Apple MPS / CUDA / CPU auto-detected)
#    --heatmap-pos-weight 80 (default) applies the ADR-017 foreground-weighted
#    heatmap MSE; the presence head is global-max-pool. `python tools/ai/selfcheck.py`
#    is a fast regression that these are in place (fails with the old plain-MSE / GAP config).
python tools/ai/train_beacon_net.py --dataset generated/ai_dataset \
    --epochs 40 --batch-size 32 --seed 26169

# 4. calibrate the presence threshold on VAL, then score TEST
python tools/ai/eval_beacon_net.py --dataset generated/ai_dataset \
    --checkpoint models/checkpoints/best.pt --max-fp-rate 0.02

# 5. export ONNX + write the model card inputs
python tools/ai/export_onnx.py --checkpoint models/checkpoints/best.pt \
    --out models/tiny_beacon_net.onnx

# 6. build the Python<->C++ parity fixture
python tools/ai/make_parity_fixture.py --onnx models/tiny_beacon_net.onnx \
    --frame generated/ai_dataset/test/test_000000.png

# 7. C++ side: build + run the AI tests and the AI validation suite
cmake --build --preset debug
ctest --preset debug
./build/debug/fsoc_ai_validation
```

## Files

| file | role |
|---|---|
| `common.py` | **frozen** preprocessing, heatmap label, soft-argmax decode, coordinate maps — mirrored bit-for-bit in `src/ai_beacon_detector.cpp` |
| `model.py` | `TinyBeaconNet` — depthwise-separable FCN, 27,282 params, global-max presence pool (ADR-017), presence + heatmap heads |
| `dataset.py` | reads `generated/ai_dataset/` (PNG + JSONL) into tensors |
| `train_beacon_net.py` | training loop, metrics, checkpoints, early stopping (ADR-017 weighted heatmap loss + `--heatmap-pos-weight`) |
| `eval_beacon_net.py` | threshold calibration on VAL, stratified TEST metrics, latency |
| `export_onnx.py` | best checkpoint → `models/tiny_beacon_net.onnx` + `.meta.json` (legacy opset-12 exporter, `dynamo=False`) |
| `make_parity_fixture.py` | writes `tests/fixtures/ai_parity/` |
| `selfcheck.py` | fast learn-ability regression (128-sample overfit: presence acc ≥ 0.97, centroid median ≤ 3 px, p90 ≤ 20 px); guards ADR-017 |

## Determinism / leakage

* Dataset seed `26169`. Train/val/test are contiguous, disjoint blocks of one
  global sample-index space; each sample's RNG seed is
  `sample_seed_for(26169, global_index)` — see `docs/21_AI_VALIDATION.md`.
* `train_beacon_net.py` and `eval_beacon_net.py` assert split-id disjointness at
  startup and abort on any overlap.
* Threshold is calibrated on **val** and frozen before **test** is scored.
