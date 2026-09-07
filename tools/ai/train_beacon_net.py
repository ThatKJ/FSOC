#!/usr/bin/env python3
"""Train TinyBeaconNet on the synthetic virtual-camera dataset.

Example:
    python tools/ai/train_beacon_net.py \
        --dataset generated/ai_dataset \
        --epochs 40 --batch-size 32 --seed 26169

Outputs (under --out-dir, default models/):
    checkpoints/best.pt   best-val checkpoint (weights + config + metrics)
    checkpoints/last.pt   final-epoch checkpoint
    training_history.json per-epoch losses + metrics
    training_summary.txt  human-readable final report

Truth labels (target_present, x_px, y_px) are used ONLY here and in evaluation —
never at inference. The network sees pixels only.
"""

from __future__ import annotations

import argparse
import json
import random
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader

from common import decode_heatmap, parameter_count
from dataset import BeaconDataset
from model import build_model


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


def pick_device(name: str) -> torch.device:
    if name != "auto":
        return torch.device(name)
    if torch.backends.mps.is_available():
        return torch.device("mps")
    if torch.cuda.is_available():
        return torch.device("cuda")
    return torch.device("cpu")


def heatmap_loss_fn(pred_logit: torch.Tensor, target_hm: torch.Tensor,
                    pos_weight: float = 0.0) -> torch.Tensor:
    """Foreground-weighted MSE between sigmoid(heatmap logits) and the Gaussian
    target heatmap.

    The 60x80 grid is ~97 % background (the Gaussian label covers ~120 cells).
    Plain unweighted MSE is minimised by a near-flat surface, so the peak is
    never shaped and centroid error stalls at tens of px (diagnosed in Stage 2 —
    DECISIONS.md ADR-017). Each cell whose target > 0 is up-weighted by
    ``1 + pos_weight``; ``pos_weight == 0`` recovers the original MSE.

    Applied to positives AND negatives (negatives have an all-zero target, so
    every cell keeps weight 1 and the surface is still pushed flat when no beacon
    is present, keeping soft-argmax meaningless once presence < thr).
    """
    prob = torch.sigmoid(pred_logit)
    if pos_weight <= 0.0:
        return nn.functional.mse_loss(prob, target_hm)
    w = 1.0 + pos_weight * (target_hm > 0).to(prob.dtype)
    return (w * (prob - target_hm) ** 2).sum() / w.sum()


@torch.no_grad()
def evaluate(model, loader, device, presence_threshold: float,
             heatmap_pos_weight: float = 0.0) -> dict:
    model.eval()
    bce = nn.BCEWithLogitsLoss()
    tot_p_loss = tot_h_loss = 0.0
    n_batches = 0

    tp = fp = tn = fn = 0
    sq_err = []          # squared centroid error (px^2), true-positive frames
    abs_err = []         # abs centroid error components (px), true-positive frames

    for x, hm, present, xy, _diff in loader:
        x, hm, present = x.to(device), hm.to(device), present.to(device)
        p_logit, h_logit = model(x)
        p_logit = p_logit.squeeze(1)
        tot_p_loss += bce(p_logit, present).item()
        tot_h_loss += heatmap_loss_fn(h_logit.squeeze(1), hm, heatmap_pos_weight).item()
        n_batches += 1

        prob = torch.sigmoid(p_logit).cpu().numpy()
        pred = prob >= presence_threshold
        gt = present.cpu().numpy() > 0.5
        h_np = h_logit.squeeze(1).cpu().numpy()
        xy_np = xy.numpy()

        for i in range(len(gt)):
            if pred[i] and gt[i]:
                tp += 1
                dx, dy, _ = decode_heatmap(h_np[i])
                ex, ey = dx - xy_np[i, 0], dy - xy_np[i, 1]
                sq_err.append(ex * ex + ey * ey)
                abs_err.append(abs(ex))
                abs_err.append(abs(ey))
            elif pred[i] and not gt[i]:
                fp += 1
            elif not pred[i] and gt[i]:
                fn += 1
            else:
                tn += 1

    precision = tp / (tp + fp) if (tp + fp) else 0.0
    recall = tp / (tp + fn) if (tp + fn) else 0.0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0
    fp_rate = fp / (fp + tn) if (fp + tn) else 0.0
    centroid_mae = float(np.mean(abs_err)) if abs_err else float("nan")
    centroid_rmse = float(np.sqrt(np.mean(sq_err))) if sq_err else float("nan")

    return {
        "presence_loss": tot_p_loss / max(n_batches, 1),
        "heatmap_loss": tot_h_loss / max(n_batches, 1),
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "fp_rate": fp_rate,
        "centroid_mae_px": centroid_mae,
        "centroid_rmse_px": centroid_rmse,
        "tp": tp, "fp": fp, "tn": tn, "fn": fn,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset", default="generated/ai_dataset")
    ap.add_argument("--out-dir", default="models")
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--batch-size", type=int, default=32)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--weight-decay", type=float, default=1e-4)
    ap.add_argument("--lambda-presence", type=float, default=1.0)
    ap.add_argument("--lambda-heatmap", type=float, default=1.0)
    ap.add_argument("--heatmap-pos-weight", type=float, default=80.0,
                    help="foreground up-weight for the heatmap MSE (cells with target>0 "
                         "get weight 1+W); 0 = original unweighted MSE. See ADR-017.")
    ap.add_argument("--presence-threshold", type=float, default=0.5,
                    help="threshold for train-time metrics only; final threshold is "
                         "calibrated on val by eval_beacon_net.py")
    ap.add_argument("--width", type=int, default=16, help="TinyBeaconNet channel width")
    ap.add_argument("--patience", type=int, default=8, help="early-stop after N epochs w/o val improvement")
    ap.add_argument("--num-workers", type=int, default=2)
    ap.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda", "mps"])
    ap.add_argument("--seed", type=int, default=26169)
    args = ap.parse_args()

    set_seed(args.seed)
    device = pick_device(args.device)
    out_dir = Path(args.out_dir)
    ckpt_dir = out_dir / "checkpoints"
    ckpt_dir.mkdir(parents=True, exist_ok=True)

    train_ds = BeaconDataset(args.dataset, "train")
    val_ds = BeaconDataset(args.dataset, "val")

    # Leakage guard: train and val ids must be disjoint.
    overlap = set(train_ds.sample_ids()) & set(val_ds.sample_ids())
    if overlap:
        raise RuntimeError(f"train/val id overlap ({len(overlap)}): {sorted(overlap)[:5]} ...")

    g = torch.Generator()
    g.manual_seed(args.seed)
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True,
                              num_workers=args.num_workers, generator=g, drop_last=False)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size, shuffle=False,
                            num_workers=args.num_workers)

    model = build_model(width=args.width).to(device)
    n_params = parameter_count(model)
    print(f"device               : {device}")
    print(f"trainable parameters : {n_params:,}  ({n_params / 1e6:.3f} M)")
    print(f"train / val samples  : {len(train_ds)} / {len(val_ds)}")
    if n_params >= 1_000_000:
        print("WARNING: parameter count exceeds the 1M design budget")

    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)
    bce = nn.BCEWithLogitsLoss()

    history = []
    best_val = float("inf")
    best_epoch = -1
    epochs_without_improvement = 0
    t0 = time.time()

    for epoch in range(1, args.epochs + 1):
        model.train()
        run_p = run_h = 0.0
        n_batches = 0
        for x, hm, present, _xy, _diff in train_loader:
            x, hm, present = x.to(device), hm.to(device), present.to(device)
            p_logit, h_logit = model(x)
            p_loss = bce(p_logit.squeeze(1), present)
            h_loss = heatmap_loss_fn(h_logit.squeeze(1), hm, args.heatmap_pos_weight)
            loss = args.lambda_presence * p_loss + args.lambda_heatmap * h_loss

            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()

            run_p += p_loss.item()
            run_h += h_loss.item()
            n_batches += 1
        sched.step()

        train_p = run_p / max(n_batches, 1)
        train_h = run_h / max(n_batches, 1)
        val = evaluate(model, val_loader, device, args.presence_threshold,
                       args.heatmap_pos_weight)
        val_total = args.lambda_presence * val["presence_loss"] + args.lambda_heatmap * val["heatmap_loss"]

        history.append({
            "epoch": epoch,
            "train_presence_loss": train_p,
            "train_heatmap_loss": train_h,
            "val_presence_loss": val["presence_loss"],
            "val_heatmap_loss": val["heatmap_loss"],
            "val_total_loss": val_total,
            "val_precision": val["precision"],
            "val_recall": val["recall"],
            "val_f1": val["f1"],
            "val_fp_rate": val["fp_rate"],
            "val_centroid_mae_px": val["centroid_mae_px"],
            "val_centroid_rmse_px": val["centroid_rmse_px"],
            "lr": sched.get_last_lr()[0],
        })
        print(f"epoch {epoch:3d}/{args.epochs}  "
              f"train(p={train_p:.4f} h={train_h:.4f})  "
              f"val(p={val['presence_loss']:.4f} h={val['heatmap_loss']:.4f} tot={val_total:.4f})  "
              f"P/R/F1={val['precision']:.3f}/{val['recall']:.3f}/{val['f1']:.3f}  "
              f"FP={val['fp_rate']:.3f}  MAE={val['centroid_mae_px']:.2f}px")

        ckpt = {
            "model_state": model.state_dict(),
            "width": args.width,
            "epoch": epoch,
            "val_metrics": val,
            "val_total_loss": val_total,
            "args": vars(args),
            "param_count": n_params,
        }
        torch.save(ckpt, ckpt_dir / "last.pt")
        if val_total < best_val - 1e-6:
            best_val = val_total
            best_epoch = epoch
            epochs_without_improvement = 0
            torch.save(ckpt, ckpt_dir / "best.pt")
        else:
            epochs_without_improvement += 1
            if epochs_without_improvement >= args.patience:
                print(f"early stopping at epoch {epoch} (no val improvement for {args.patience})")
                break

    wall = time.time() - t0
    (out_dir / "training_history.json").write_text(json.dumps(history, indent=2))

    best = torch.load(ckpt_dir / "best.pt", map_location="cpu")
    bm = best["val_metrics"]
    summary = (
        f"TinyBeaconNet training summary\n"
        f"=============================\n"
        f"dataset            : {args.dataset}\n"
        f"seed               : {args.seed}\n"
        f"device             : {device}\n"
        f"trainable params   : {n_params:,} ({n_params/1e6:.3f} M)\n"
        f"epochs run         : {history[-1]['epoch']} (best val at epoch {best_epoch})\n"
        f"wall time          : {wall:.1f} s\n"
        f"lambda p / h       : {args.lambda_presence} / {args.lambda_heatmap}\n"
        f"heatmap pos-weight : {args.heatmap_pos_weight}  (foreground-weighted MSE; ADR-017)\n"
        f"presence pooling   : global max\n"
        f"checkpoint criterion: min val_total = lambda_p*BCE(presence) + lambda_h*weighted_MSE(heatmap)\n"
        f"\n"
        f"best-val metrics (threshold {args.presence_threshold}):\n"
        f"  presence precision : {bm['precision']:.4f}\n"
        f"  presence recall    : {bm['recall']:.4f}\n"
        f"  presence F1        : {bm['f1']:.4f}\n"
        f"  false-positive rate: {bm['fp_rate']:.4f}\n"
        f"  centroid MAE       : {bm['centroid_mae_px']:.4f} px\n"
        f"  centroid RMSE      : {bm['centroid_rmse_px']:.4f} px\n"
        f"  confusion tp/fp/tn/fn: {bm['tp']}/{bm['fp']}/{bm['tn']}/{bm['fn']}\n"
        f"\n"
        f"next: python tools/ai/eval_beacon_net.py --dataset {args.dataset} "
        f"--checkpoint {ckpt_dir/'best.pt'}\n"
        f"      python tools/ai/export_onnx.py --checkpoint {ckpt_dir/'best.pt'}\n"
    )
    (out_dir / "training_summary.txt").write_text(summary)
    print("\n" + summary)


if __name__ == "__main__":
    main()
