#!/usr/bin/env python3
"""Evaluate TinyBeaconNet + calibrate the presence threshold.

Threshold calibration is done on the VALIDATION split and frozen BEFORE the test
split is ever scored (docs/21, Section 27/28 of the AI phase brief). The chosen
threshold is written to models/threshold.json and consumed by the C++ detector.

    python tools/ai/eval_beacon_net.py \
        --dataset generated/ai_dataset \
        --checkpoint models/checkpoints/best.pt \
        --max-fp-rate 0.02

Outputs (under --out-dir, default models/):
    threshold.json     chosen threshold + the full val sweep
    eval_report.json   test-split metrics, overall + stratified
    eval_report.txt    human-readable
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader

from common import decode_heatmap
from dataset import BeaconDataset
from model import build_model


@torch.no_grad()
def collect(model, ds, device, batch_size=32):
    """Run the model over a dataset; return per-sample arrays + joined manifest."""
    loader = DataLoader(ds, batch_size=batch_size, shuffle=False, num_workers=2)
    probs, gts, dec_err, present_pred_xy = [], [], [], []
    for x, _hm, present, xy, _diff in loader:
        p_logit, h_logit = model(x.to(device))
        prob = torch.sigmoid(p_logit.squeeze(1)).cpu().numpy()
        h_np = h_logit.squeeze(1).cpu().numpy()
        xy_np = xy.numpy()
        gt = present.numpy()
        for i in range(len(gt)):
            probs.append(float(prob[i]))
            gts.append(float(gt[i]))
            dx, dy, _ = decode_heatmap(h_np[i])
            if gt[i] > 0.5:
                dec_err.append(np.hypot(dx - xy_np[i, 0], dy - xy_np[i, 1]))
            else:
                dec_err.append(np.nan)
            present_pred_xy.append((dx, dy))
    return (np.array(probs), np.array(gts), np.array(dec_err),
            ds.records, present_pred_xy)


def sweep_thresholds(probs, gts, grid):
    rows = []
    pos = gts > 0.5
    neg = ~pos
    for thr in grid:
        pred = probs >= thr
        tp = int(np.sum(pred & pos))
        fp = int(np.sum(pred & neg))
        fn = int(np.sum(~pred & pos))
        tn = int(np.sum(~pred & neg))
        precision = tp / (tp + fp) if (tp + fp) else 0.0
        recall = tp / (tp + fn) if (tp + fn) else 0.0
        f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0
        fp_rate = fp / (fp + tn) if (fp + tn) else 0.0
        rows.append({"threshold": round(float(thr), 4), "precision": precision,
                     "recall": recall, "f1": f1, "fp_rate": fp_rate,
                     "tp": tp, "fp": fp, "tn": tn, "fn": fn})
    return rows


def stratum_metrics(probs, gts, dec_err, records, mask, thr):
    idx = np.where(mask)[0]
    if len(idx) == 0:
        return None
    pos = idx[gts[idx] > 0.5]
    neg = idx[gts[idx] <= 0.5]
    det_rate = float(np.mean(probs[pos] >= thr)) if len(pos) else float("nan")
    fp_rate = float(np.mean(probs[neg] >= thr)) if len(neg) else float("nan")
    tp_mask = pos[probs[pos] >= thr]
    mae = float(np.nanmean(dec_err[tp_mask])) if len(tp_mask) else float("nan")
    return {"n": int(len(idx)), "n_pos": int(len(pos)), "n_neg": int(len(neg)),
            "detection_rate": det_rate, "fp_rate": fp_rate, "centroid_mae_px": mae}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset", default="generated/ai_dataset")
    ap.add_argument("--checkpoint", default="models/checkpoints/best.pt")
    ap.add_argument("--out-dir", default="models")
    ap.add_argument("--max-fp-rate", type=float, default=0.02,
                    help="pick the lowest threshold whose VAL fp-rate is <= this, "
                         "breaking ties by highest recall")
    ap.add_argument("--device", default="cpu", choices=["cpu", "cuda", "mps"])
    ap.add_argument("--latency-iters", type=int, default=300)
    args = ap.parse_args()

    device = torch.device(args.device)
    ckpt = torch.load(args.checkpoint, map_location="cpu")
    model = build_model(width=ckpt.get("width", 16)).to(device)
    model.load_state_dict(ckpt["model_state"])
    model.eval()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # ---- calibrate on VAL ------------------------------------------------
    val_ds = BeaconDataset(args.dataset, "val")
    v_probs, v_gts, _v_err, _v_rec, _ = collect(model, val_ds, device)
    grid = np.round(np.arange(0.05, 0.96, 0.05), 4)
    sweep = sweep_thresholds(v_probs, v_gts, grid)

    feasible = [r for r in sweep if r["fp_rate"] <= args.max_fp_rate]
    if feasible:
        chosen = max(feasible, key=lambda r: (r["recall"], -r["threshold"]))
    else:
        chosen = min(sweep, key=lambda r: r["fp_rate"])  # fall back to lowest FP
        print(f"WARNING: no threshold meets fp_rate <= {args.max_fp_rate}; "
              f"using lowest-FP threshold {chosen['threshold']}")
    thr = chosen["threshold"]

    (out_dir / "threshold.json").write_text(json.dumps({
        "presence_threshold": thr,
        "selection_rule": f"min threshold with VAL fp_rate <= {args.max_fp_rate}, tie-break max recall",
        "chosen_row": chosen,
        "val_sweep": sweep,
        "checkpoint": str(args.checkpoint),
    }, indent=2))
    print(f"calibrated presence threshold = {thr}  "
          f"(val P/R/F1={chosen['precision']:.3f}/{chosen['recall']:.3f}/{chosen['f1']:.3f}, "
          f"FP={chosen['fp_rate']:.3f})")

    # ---- score TEST at the frozen threshold ----------------------------
    test_ds = BeaconDataset(args.dataset, "test")
    overlap = set(val_ds.sample_ids()) & set(test_ds.sample_ids())
    if overlap:
        raise RuntimeError(f"val/test id overlap: {len(overlap)}")
    t_probs, t_gts, t_err, t_rec, _ = collect(model, test_ds, device)

    overall = stratum_metrics(t_probs, t_gts, t_err, t_rec,
                              np.ones(len(t_gts), dtype=bool), thr)

    def rec_attr(name, default=None):
        return np.array([r.get(name, default) for r in t_rec])

    diff = rec_attr("difficulty", 0.0).astype(float)
    peak = rec_attr("beacon_peak", 0.0).astype(float)
    rsig = rec_attr("read_sigma", 1.0).astype(float)
    snr = np.divide(peak, np.maximum(rsig, 1e-6))
    optmode = rec_attr("optical_mode", "none")
    bright = rec_attr("has_bright_distractor", False).astype(bool)

    strata = {
        "clean_easy (difficulty<0.2)": diff < 0.2,
        "mid (0.2<=difficulty<0.5)": (diff >= 0.2) & (diff < 0.5),
        "hard (difficulty>=0.5)": diff >= 0.5,
        "low_snr (snr<15)": snr < 15.0,
        "bright_distractor": bright,
        "optical_none": optmode == "none",
        "optical_gaussian_blur": optmode == "gaussian_blur",
        "optical_defocus": optmode == "defocus",
        "optical_motion_blur": optmode == "motion_blur",
        "target_absent": t_gts <= 0.5,
    }
    strat_out = {k: stratum_metrics(t_probs, t_gts, t_err, t_rec, m, thr) for k, m in strata.items()}

    # ---- latency (Python/torch CPU; the authoritative number is the C++
    #      OpenCV-DNN path measured by fsoc_ai_validation) ----------------
    dummy = torch.zeros(1, 1, 240, 320)
    for _ in range(10):
        model(dummy)
    times = []
    for _ in range(args.latency_iters):
        t0 = time.perf_counter()
        model(dummy)
        times.append((time.perf_counter() - t0) * 1000.0)
    times = np.array(times)
    latency = {
        "mean_ms": float(times.mean()),
        "median_ms": float(np.median(times)),
        "p95_ms": float(np.percentile(times, 95)),
        "max_ms": float(times.max()),
        "throughput_fps": float(1000.0 / times.mean()),
        "note": "torch CPU single-thread; not the deployed OpenCV-DNN C++ latency",
    }

    report = {
        "checkpoint": str(args.checkpoint),
        "presence_threshold": thr,
        "param_count": ckpt.get("param_count"),
        "test_overall": overall,
        "test_strata": strat_out,
        "python_latency": latency,
    }
    (out_dir / "eval_report.json").write_text(json.dumps(report, indent=2))

    lines = [
        "TinyBeaconNet evaluation (test split)",
        "====================================",
        f"checkpoint          : {args.checkpoint}",
        f"presence threshold  : {thr}  (calibrated on val, fp_rate<={args.max_fp_rate})",
        f"param count         : {ckpt.get('param_count')}",
        "",
        f"OVERALL  det_rate={overall['detection_rate']:.4f}  fp_rate={overall['fp_rate']:.4f}  "
        f"centroid_MAE={overall['centroid_mae_px']:.3f}px  (n={overall['n']})",
        "",
        "stratified:",
    ]
    for k, v in strat_out.items():
        if v is None:
            lines.append(f"  {k:<32s}  (no samples)")
        else:
            lines.append(f"  {k:<32s}  n={v['n']:<5d} det={v['detection_rate']:.3f}  "
                         f"fp={v['fp_rate']:.3f}  MAE={v['centroid_mae_px']:.3f}px")
    lines += [
        "",
        f"python latency (torch CPU): mean={latency['mean_ms']:.2f}ms  "
        f"p95={latency['p95_ms']:.2f}ms  max={latency['max_ms']:.2f}ms  "
        f"({latency['throughput_fps']:.0f} fps)",
        "NOTE: deploy latency is measured by the C++ OpenCV-DNN path (fsoc_ai_validation).",
    ]
    (out_dir / "eval_report.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
