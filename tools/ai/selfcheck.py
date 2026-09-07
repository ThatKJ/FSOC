#!/usr/bin/env python3
"""Fast training-pipeline regression for TinyBeaconNet (ADR-017).

Overfits the real model on a fixed 128-sample real subset of
`generated/ai_dataset` (1500 steps) and asserts it actually learns:

    presence accuracy   >= 0.97
    centroid error       median <= 3 px   AND   p90 <= 20 px

This is the guard for the Stage-2 loss/pooling fix (ADR-017): with the pre-ADR-017
config (plain unweighted heatmap MSE + global-average-pool presence head) the
128-sample overfit still leaves p90 in the ~150-300 px range and this check FAILS
(observed p90 4.8 px with ADR-017). It is deliberately NOT wired into CTest
(needs the .venv-ai + the generated dataset); run it after touching `model.py`
or `train_beacon_net.py`:

    source .venv-ai/bin/activate
    python tools/ai/selfcheck.py            # exit 0 = pass

Runs in ~90 s on Apple MPS.
"""
from __future__ import annotations
import argparse
import sys
import time

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Subset

from common import decode_heatmap
from dataset import BeaconDataset
from model import build_model
from train_beacon_net import heatmap_loss_fn, set_seed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset", default="generated/ai_dataset")
    ap.add_argument("--n", type=int, default=128, help="training subset size")
    ap.add_argument("--steps", type=int, default=1500)
    ap.add_argument("--seed", type=int, default=26169)
    ap.add_argument("--heatmap-pos-weight", type=float, default=80.0)
    ap.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda", "mps"])
    ap.add_argument("--max-median-px", type=float, default=3.0)
    ap.add_argument("--max-p90-px", type=float, default=20.0)
    ap.add_argument("--min-presence-acc", type=float, default=0.97)
    args = ap.parse_args()

    set_seed(args.seed)
    if args.device == "auto":
        dev = torch.device("mps" if torch.backends.mps.is_available()
                           else "cuda" if torch.cuda.is_available() else "cpu")
    else:
        dev = torch.device(args.device)

    full = BeaconDataset(args.dataset, "train")
    # fixed, class-mixed subset (~75/25 like the dataset)
    pos = [i for i, r in enumerate(full.records) if r["target_present"]]
    neg = [i for i, r in enumerate(full.records) if not r["target_present"]]
    n_pos = int(round(args.n * 0.75))
    sel = pos[:n_pos] + neg[:args.n - n_pos]
    ds = Subset(full, sel)
    loader = DataLoader(ds, batch_size=32, shuffle=True, num_workers=0,
                        generator=torch.Generator().manual_seed(args.seed))

    model = build_model(width=16).to(dev)
    opt = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-4)
    bce = nn.BCEWithLogitsLoss()

    t0 = time.time()
    step = 0
    model.train()
    while step < args.steps:
        for x, hm, present, _xy, _d in loader:
            x, hm, present = x.to(dev), hm.to(dev), present.to(dev)
            pl, hl = model(x)
            loss = bce(pl.squeeze(1), present) + heatmap_loss_fn(hl.squeeze(1), hm, args.heatmap_pos_weight)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            step += 1
            if step >= args.steps:
                break

    # evaluate on the same subset (this is a learn-ability probe, not a generalisation test)
    model.eval()
    probs, gts, errs = [], [], []
    with torch.no_grad():
        for x, hm, present, xy, _d in DataLoader(ds, batch_size=64, shuffle=False):
            pl, hl = model(x.to(dev))
            p = torch.sigmoid(pl.squeeze(1)).cpu().numpy()
            h = hl.squeeze(1).cpu().numpy()
            xy = xy.numpy()
            g = present.numpy()
            for i in range(len(g)):
                probs.append(float(p[i]))
                gts.append(float(g[i]))
                if g[i] > 0.5:
                    dx, dy, _ = decode_heatmap(h[i])
                    errs.append(float(np.hypot(dx - xy[i, 0], dy - xy[i, 1])))
    probs = np.array(probs)
    gts = np.array(gts)
    errs = np.array(errs)
    acc = float(np.mean((probs >= 0.5) == (gts >= 0.5)))
    med = float(np.median(errs))
    p90 = float(np.percentile(errs, 90))
    dt = time.time() - t0

    print(f"device={dev}  subset={len(sel)} ({n_pos} pos)  steps={args.steps}  wall={dt:.1f}s")
    print(f"presence accuracy : {acc:.3f}   (need >= {args.min_presence_acc})")
    print(f"centroid median   : {med:.2f} px   (need <= {args.max_median_px})")
    print(f"centroid p90      : {p90:.2f} px   (need <= {args.max_p90_px})")
    print(f"centroid mean/max : {errs.mean():.2f} / {errs.max():.1f} px")

    ok = acc >= args.min_presence_acc and med <= args.max_median_px and p90 <= args.max_p90_px
    print(f"\nSELFCHECK: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
