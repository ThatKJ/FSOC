#!/usr/bin/env python3
"""Build the Python<->C++ parity fixture consumed by fsoc_ai_perception_tests.

    python tools/ai/make_parity_fixture.py \
        --onnx models/tiny_beacon_net.onnx \
        --frame generated/ai_dataset/test/test_000000.png

Writes tests/fixtures/ai_parity/ (committed — all small):
    input.png             the raw CV_8UC1 640x480 frame
    preprocessed_ref.png  round(preprocess(frame) * 255) as CV_8UC1 320x240
                          -> C++ preprocessing must match within +/- 1 count
    expected.json         presence logit/prob, heatmap min/max/mean, a 5x5 grid
                          of heatmap-logit samples, decoded centroid, tolerances

The 'expected' values are produced by onnxruntime, so the C++ OpenCV-DNN path
(same ONNX graph) should reproduce them tightly.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError as exc:  # pragma: no cover
    raise SystemExit("opencv-python required: pip install opencv-python") from exc

from common import (
    HM_H, HM_W, IN_H, IN_W, ONNX_INPUT_NAME, decode_heatmap, preprocess,
)

FIXTURE_DIR = Path("tests/fixtures/ai_parity")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--onnx", default="models/tiny_beacon_net.onnx")
    ap.add_argument("--frame", default="generated/ai_dataset/test/test_000000.png")
    ap.add_argument("--out-dir", default=str(FIXTURE_DIR))
    args = ap.parse_args()

    import onnxruntime as ort

    frame = cv2.imread(args.frame, cv2.IMREAD_GRAYSCALE)
    if frame is None:
        raise SystemExit(f"cannot read {args.frame}")
    if frame.shape != (480, 640):
        raise SystemExit(f"expected 640x480 frame, got {frame.shape[::-1]}")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out_dir / "input.png"), frame)

    x = preprocess(frame)                       # [1,1,240,320] float32 in [0,1]
    ref_u8 = np.clip(np.round(x[0, 0] * 255.0), 0, 255).astype(np.uint8)
    cv2.imwrite(str(out_dir / "preprocessed_ref.png"), ref_u8)

    sess = ort.InferenceSession(args.onnx, providers=["CPUExecutionProvider"])
    p_logit, h_logit = sess.run(None, {ONNX_INPUT_NAME: x})
    p_logit = float(np.asarray(p_logit).reshape(-1)[0])
    h_logit = np.asarray(h_logit).reshape(HM_H, HM_W).astype(np.float64)

    p_prob = 1.0 / (1.0 + np.exp(-p_logit))
    dx, dy, peak_conf = decode_heatmap(h_logit)

    ys = np.linspace(0, HM_H - 1, 5).round().astype(int)
    xs = np.linspace(0, HM_W - 1, 5).round().astype(int)
    grid = [[float(h_logit[iy, ix]) for ix in xs] for iy in ys]

    expected = {
        "frame_source": args.frame,
        "onnx": args.onnx,
        "input_shape": [1, 1, IN_H, IN_W],
        "heatmap_shape": [HM_H, HM_W],
        "presence_logit": p_logit,
        "presence_prob": p_prob,
        "heatmap_logit_min": float(h_logit.min()),
        "heatmap_logit_max": float(h_logit.max()),
        "heatmap_logit_mean": float(h_logit.mean()),
        "heatmap_logit_grid_rows": ys.tolist(),
        "heatmap_logit_grid_cols": xs.tolist(),
        "heatmap_logit_grid": grid,
        "decoded_x_px": dx,
        "decoded_y_px": dy,
        "decoded_peak_confidence": peak_conf,
        "tolerances": {
            "preprocess_abs_count": 1.0,
            "presence_logit_abs": 2e-3,
            "heatmap_logit_abs": 3e-3,
            "decoded_px_abs": 0.25,
        },
    }
    (out_dir / "expected.json").write_text(json.dumps(expected, indent=2))
    print(f"wrote fixture to {out_dir}/  (presence_prob={p_prob:.4f}, "
          f"decoded=({dx:.2f},{dy:.2f}))")


if __name__ == "__main__":
    main()
