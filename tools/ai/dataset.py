"""BeaconDataset — reads the C++-generated synthetic dataset.

Layout (produced by `generate_ai_dataset`, see docs/20):
    <root>/<split>/<split>_NNNNNN.png          CV_8UC1 640x480
    <root>/metadata/<split>.jsonl              one label record per line
    <root>/metadata/dataset.json               reproducibility manifest

Each __getitem__ returns:
    input      float32 [1, 240, 320] in [0, 1]
    heatmap    float32 [60, 80]  (Gaussian at the beacon; zeros for negatives)
    present    float32 scalar (1.0 / 0.0)
    label_xy   float32 [2]  (x_px, y_px in original pixels; (-1,-1) for negatives)
    difficulty float32 scalar  (stratified-reporting proxy; never fed to the net)
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError:  # pragma: no cover
    cv2 = None

import torch
from torch.utils.data import Dataset

from common import empty_heatmap, gaussian_heatmap, preprocess


class BeaconDataset(Dataset):
    def __init__(self, root: str | Path, split: str) -> None:
        if cv2 is None:
            raise RuntimeError("opencv-python is required; pip install opencv-python")
        self.root = Path(root)
        self.split = split
        manifest = self.root / "metadata" / f"{split}.jsonl"
        if not manifest.is_file():
            raise FileNotFoundError(
                f"{manifest} not found — run generate_ai_dataset --out {self.root} first"
            )
        with manifest.open() as fh:
            self.records = [json.loads(line) for line in fh if line.strip()]
        if not self.records:
            raise RuntimeError(f"{manifest} is empty")

    def __len__(self) -> int:
        return len(self.records)

    def sample_ids(self) -> list[str]:
        return [r["id"] for r in self.records]

    def __getitem__(self, idx: int):
        rec = self.records[idx]
        png = self.root / self.split / f"{rec['id']}.png"
        frame = cv2.imread(str(png), cv2.IMREAD_GRAYSCALE)
        if frame is None:
            raise FileNotFoundError(f"cannot read {png}")

        x = preprocess(frame)[0]  # [1, 240, 320]

        present = bool(rec["target_present"])
        if present:
            xp, yp = float(rec["x_px"]), float(rec["y_px"])
            hm = gaussian_heatmap(xp, yp)
        else:
            xp, yp = -1.0, -1.0
            hm = empty_heatmap()

        return (
            torch.from_numpy(x),
            torch.from_numpy(hm),
            torch.tensor(1.0 if present else 0.0, dtype=torch.float32),
            torch.tensor([xp, yp], dtype=torch.float32),
            torch.tensor(float(rec.get("difficulty", 0.0)), dtype=torch.float32),
        )
