"""Group-based train/validation/test split for real-session recordings (G2).

Splits by RECORDING (capture group), never by frame -- every frame, crop, and
augmented descendant of one recording stays in the same partition. Persists one
JSON split manifest so the same partition is reused across every training/eval
run rather than re-derived (and potentially drifting) each time one is needed.

Usage:
    python tools/ai/real_dataset_split.py \
        --real-sessions-root generated/real_sessions \
        --out generated/real_dataset_splits/split_v1.json
"""

from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

Split = str  # "train" | "val" | "test"


def assign_splits(
    recording_ids: list[str],
    train_frac: float = 0.6,
    val_frac: float = 0.2,
    test_frac: float = 0.2,
    seed: int = 26169,
) -> dict[str, Split]:
    """Deterministically assigns each recording id to train/val/test by GROUP
    count, not frame count.

    Raises ValueError if there are too few groups to populate every requested
    non-empty split. A forced split with an empty partition would silently hide
    "there is not enough real data yet" as if evaluation had actually run --
    this project's own rules treat that as a real problem to surface, not a
    cosmetic one to paper over.
    """
    if abs(train_frac + val_frac + test_frac - 1.0) > 1e-6:
        raise ValueError("train_frac + val_frac + test_frac must sum to 1.0")
    n = len(recording_ids)
    if n == 0:
        raise ValueError("no recordings to split")
    if len(set(recording_ids)) != n:
        raise ValueError("recording_ids contains duplicates")

    non_zero_splits = sum(1 for f in (train_frac, val_frac, test_frac) if f > 0)
    if n < non_zero_splits:
        raise ValueError(
            f"only {n} recording group(s) available, but {non_zero_splits} non-empty "
            "splits were requested -- collect more independent recordings before "
            "splitting (a forced split would leave a split empty)."
        )

    # Sort first so shuffling is reproducible independent of the CALLER's input
    # order (e.g. directory listing order, which is not guaranteed stable).
    shuffled = sorted(recording_ids)
    random.Random(seed).shuffle(shuffled)

    n_train = max(1, round(n * train_frac)) if train_frac > 0 else 0
    n_val = max(1, round(n * val_frac)) if val_frac > 0 else 0
    n_test = n - n_train - n_val
    if test_frac > 0 and n_test < 1:
        # Minimal rebalance: pull one group back from whichever of train/val is
        # larger, rather than leaving the requested test split empty.
        if n_train >= n_val and n_train > 1:
            n_train -= 1
        elif n_val > 1:
            n_val -= 1
        n_test = n - n_train - n_val

    assignment: dict[str, Split] = {}
    idx = 0
    for rid in shuffled[idx : idx + n_train]:
        assignment[rid] = "train"
    idx += n_train
    for rid in shuffled[idx : idx + n_val]:
        assignment[rid] = "val"
    idx += n_val
    for rid in shuffled[idx:]:
        assignment[rid] = "test"
    return assignment


def save_split_manifest(path: str | Path, assignment: dict[str, Split], *, note: str = "") -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    doc = {"schemaVersion": 1, "note": note, "assignment": assignment}
    Path(path).write_text(json.dumps(doc, indent=2))


def load_split_manifest(path: str | Path) -> dict[str, Split]:
    doc = json.loads(Path(path).read_text())
    return doc["assignment"]


def recordings_for_split(assignment: dict[str, Split], split: Split) -> list[str]:
    return [rid for rid, s in assignment.items() if s == split]


def discover_recording_ids(real_sessions_root: str | Path) -> list[str]:
    root = Path(real_sessions_root)
    if not root.is_dir():
        return []
    return sorted(p.name for p in root.iterdir() if p.is_dir() and (p / "manifest.json").is_file())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--real-sessions-root", default="generated/real_sessions")
    ap.add_argument("--out", required=True, help="path to write the split manifest JSON")
    ap.add_argument("--train-frac", type=float, default=0.6)
    ap.add_argument("--val-frac", type=float, default=0.2)
    ap.add_argument("--test-frac", type=float, default=0.2)
    ap.add_argument("--seed", type=int, default=26169)
    args = ap.parse_args()

    recording_ids = discover_recording_ids(args.real_sessions_root)
    if not recording_ids:
        print(f"no recordings with a manifest.json found under {args.real_sessions_root}")
        return 1

    try:
        assignment = assign_splits(
            recording_ids, args.train_frac, args.val_frac, args.test_frac, args.seed
        )
    except ValueError as e:
        print(f"cannot split: {e}")
        return 1

    save_split_manifest(
        args.out, assignment, note=f"{len(recording_ids)} recording group(s) from {args.real_sessions_root}"
    )
    for split in ("train", "val", "test"):
        ids = recordings_for_split(assignment, split)
        print(f"{split}: {len(ids)} recording(s) -> {ids}")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
