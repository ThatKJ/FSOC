#!/usr/bin/env python3
"""Export the best TinyBeaconNet checkpoint to ONNX for C++ OpenCV-DNN inference.

    python tools/ai/export_onnx.py \
        --checkpoint models/checkpoints/best.pt \
        --out models/tiny_beacon_net.onnx

Produces:
    models/tiny_beacon_net.onnx        the model (committed — small)
    models/tiny_beacon_net.meta.json   frozen interface constants + provenance

The graph outputs RAW LOGITS for both heads (sigmoid is applied outside the
graph in both Python eval and C++). Batch axis is dynamic; spatial dims are
fixed at the frozen 320x240 input / 80x60 heatmap.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path

import numpy as np
import torch

from common import (
    APPLY_SIGMOID_IN_GRAPH, HEATMAP_SIGMA_CELLS, HM_H, HM_W, IN_H, IN_W,
    INPUT_STRIDE, ONNX_HEATMAP_OUTPUT, ONNX_INPUT_NAME, ONNX_OPSET,
    ONNX_PRESENCE_OUTPUT, ORIG_H, ORIG_W, parameter_count,
)
from model import build_model


def git_commit() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    except Exception:
        return "unknown"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--checkpoint", default="models/checkpoints/best.pt")
    ap.add_argument("--out", default="models/tiny_beacon_net.onnx")
    ap.add_argument("--dataset-seed", type=int, default=26169)
    ap.add_argument("--rtol", type=float, default=1e-4)
    args = ap.parse_args()

    ckpt = torch.load(args.checkpoint, map_location="cpu")
    width = ckpt.get("width", 16)
    model = build_model(width=width)
    model.load_state_dict(ckpt["model_state"])
    model.eval()
    n_params = parameter_count(model)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.zeros(1, 1, IN_H, IN_W)

    torch.onnx.export(
        model, dummy, str(out_path),
        input_names=[ONNX_INPUT_NAME],
        output_names=[ONNX_PRESENCE_OUTPUT, ONNX_HEATMAP_OUTPUT],
        opset_version=ONNX_OPSET,
        dynamic_axes={ONNX_INPUT_NAME: {0: "batch"},
                      ONNX_PRESENCE_OUTPUT: {0: "batch"},
                      ONNX_HEATMAP_OUTPUT: {0: "batch"}},
        do_constant_folding=True,
        # Pin the legacy TorchScript exporter: it emits the fixed opset-12 graph
        # (Conv/Gemm/MaxPool/Relu, BN folded) that the C++ OpenCV-DNN path and the
        # parity fixture are built against. torch>=2.9 defaults to the dynamo
        # exporter, which needs onnxscript and produces a different graph.
        dynamo=False,
    )
    size_kb = out_path.stat().st_size / 1024.0
    print(f"wrote {out_path}  ({size_kb:.1f} KiB, {n_params:,} params)")

    # ---- structural check ---------------------------------------------
    try:
        import onnx
        onnx.checker.check_model(onnx.load(str(out_path)))
        print("onnx.checker: OK")
    except ImportError:
        print("onnx not installed — skipping onnx.checker")

    # ---- numerical parity: torch vs onnxruntime ----------------------
    try:
        import onnxruntime as ort
        sess = ort.InferenceSession(str(out_path), providers=["CPUExecutionProvider"])
        rng = np.random.default_rng(0)
        max_diff_p = max_diff_h = 0.0
        for _ in range(8):
            x = rng.random((1, 1, IN_H, IN_W), dtype=np.float32)
            with torch.no_grad():
                tp, th = model(torch.from_numpy(x))
            op, oh = sess.run(None, {ONNX_INPUT_NAME: x})
            max_diff_p = max(max_diff_p, float(np.abs(tp.numpy() - op).max()))
            max_diff_h = max(max_diff_h, float(np.abs(th.numpy() - oh).max()))
        print(f"torch vs onnxruntime: max|Δ presence|={max_diff_p:.2e}  "
              f"max|Δ heatmap|={max_diff_h:.2e}")
        if max(max_diff_p, max_diff_h) > args.rtol:
            raise SystemExit(f"ONNX parity check failed (> {args.rtol})")
    except ImportError:
        print("onnxruntime not installed — skipping torch/ORT parity check")

    meta = {
        "model": "TinyBeaconNet",
        "onnx_file": out_path.name,
        "param_count": n_params,
        "width": width,
        "input": {"name": "input", "shape": [1, 1, IN_H, IN_W], "layout": "NCHW",
                  "dtype": "float32", "range": [0.0, 1.0]},
        "outputs": {
            "presence_logit": {"shape": [1, 1], "activation_outside_graph": "sigmoid"},
            "heatmap_logit": {"shape": [1, 1, HM_H, HM_W], "activation_outside_graph": "sigmoid"},
        },
        "apply_sigmoid_in_graph": APPLY_SIGMOID_IN_GRAPH,
        "opset": ONNX_OPSET,
        "geometry": {
            "orig_w": ORIG_W, "orig_h": ORIG_H, "in_w": IN_W, "in_h": IN_H,
            "hm_w": HM_W, "hm_h": HM_H, "input_stride": INPUT_STRIDE,
            "heatmap_sigma_cells": HEATMAP_SIGMA_CELLS,
            "coord_map": "x_orig = (x_hm + 0.5) * stride - 0.5",
        },
        "preprocessing": "cv2.resize INTER_AREA to 320x240, float32, /255.0, NCHW",
        "provenance": {
            "trained_epoch": ckpt.get("epoch"),
            "val_metrics": ckpt.get("val_metrics"),
            "dataset_seed": args.dataset_seed,
            "git_commit": git_commit(),
            "exported_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "training_note": "synthetic virtual-camera imagery only (domain-randomized)",
        },
    }
    meta_path = out_path.with_suffix(".meta.json")
    meta_path.write_text(json.dumps(meta, indent=2))
    print(f"wrote {meta_path}")


if __name__ == "__main__":
    main()
