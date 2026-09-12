#!/usr/bin/env python3
"""Plumbing checks for real_dataset.py / real_dataset_split.py (G2/G3).

Builds small, CLEARLY LABELED FIXTURE recordings under a temp directory --
synthetic frames constructed just to exercise the validation/transform/split
logic. These fixtures are not collected real camera data and must never be
treated as such; they exist only to prove the plumbing is correct before any
real reviewed recording is used for training. Not wired into CTest (needs
opencv-python/torch, same as selfcheck.py) -- run manually:

    .venv-ai/bin/python3 tools/ai/real_dataset_tests.py
"""

from __future__ import annotations

import json
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None

from common import ORIG_H, ORIG_W, decode_heatmap
from real_dataset import RealBeaconDataset, RecordingValidationError, collect_reviewed_samples
from real_dataset_split import assign_splits, discover_recording_ids, load_split_manifest, save_split_manifest

failures = 0


def check(condition: bool, message: str) -> None:
    global failures
    if not condition:
        failures += 1
        print(f"FAIL: {message}")


def check_raises(fn, exc_type, message: str) -> None:
    try:
        fn()
    except exc_type:
        return
    except Exception as e:  # noqa: BLE001
        check(False, f"{message} (raised {type(e).__name__} instead of {exc_type.__name__})")
        return
    check(False, f"{message} (did not raise)")


FIXTURE_RAW_W, FIXTURE_RAW_H = 1280, 720  # deliberately NOT 640x480, to exercise the resize path


def make_fixture_recording(
    root: Path,
    recording_id: str,
    *,
    labels: dict | None = None,
    label_coordinate_space: str = "raw",
    n_frames: int = 3,
    skip_manifest: bool = False,
    skip_labels: bool = False,
    skip_frame_index: int | None = None,
) -> Path:
    """A CLEARLY LABELED FIXTURE recording -- solid-gray synthetic frames, not
    real camera captures. Mirrors fsoc::RealSessionRecorder's on-disk layout."""
    rec_dir = root / recording_id
    (rec_dir / "frames").mkdir(parents=True, exist_ok=True)
    for i in range(n_frames):
        if skip_frame_index == i:
            continue
        frame = np.full((FIXTURE_RAW_H, FIXTURE_RAW_W), 60 + i, dtype=np.uint8)
        cv2.imwrite(str(rec_dir / "frames" / f"frame_{i}.jpg"), frame)

    if not skip_manifest:
        manifest = {
            "schemaVersion": 1,
            "recordingId": recording_id,
            "sessionId": f"session-for-{recording_id}",
            "rawWidthPx": FIXTURE_RAW_W,
            "rawHeightPx": FIXTURE_RAW_H,
            "recordedFrameCount": n_frames,
        }
        (rec_dir / "manifest.json").write_text(json.dumps(manifest))

    if not skip_labels:
        doc = {
            "schemaVersion": 1,
            "recordingId": recording_id,
            "sessionId": f"session-for-{recording_id}",
            "labelCoordinateSpace": label_coordinate_space,
            "labels": labels or {},
        }
        (rec_dir / "labels.json").write_text(json.dumps(doc))

    return rec_dir


def test_missing_manifest_raises(root: Path) -> None:
    rec_dir = make_fixture_recording(root, "rec_missing_manifest", skip_manifest=True)
    check_raises(
        lambda: collect_reviewed_samples(rec_dir),
        RecordingValidationError,
        "missing manifest.json must raise",
    )


def test_missing_labels_raises(root: Path) -> None:
    rec_dir = make_fixture_recording(root, "rec_missing_labels", skip_labels=True)
    check_raises(
        lambda: collect_reviewed_samples(rec_dir),
        RecordingValidationError,
        "missing labels.json must raise",
    )


def test_wrong_coordinate_space_raises(root: Path) -> None:
    rec_dir = make_fixture_recording(root, "rec_wrong_space", label_coordinate_space="preprocessed")
    check_raises(
        lambda: collect_reviewed_samples(rec_dir),
        RecordingValidationError,
        "labelCoordinateSpace != 'raw' must raise",
    )


def test_present_without_center_raises(root: Path) -> None:
    rec_dir = make_fixture_recording(
        root,
        "rec_present_no_center",
        labels={"0": {"presence": "present", "centerXPx": None, "centerYPx": None, "reviewed": True}},
    )
    check_raises(
        lambda: collect_reviewed_samples(rec_dir),
        RecordingValidationError,
        "presence=present with no center must raise",
    )


def test_center_outside_raw_bounds_raises(root: Path) -> None:
    rec_dir = make_fixture_recording(
        root,
        "rec_out_of_bounds",
        labels={
            "0": {
                "presence": "present",
                "centerXPx": FIXTURE_RAW_W + 50,  # outside this recording's own raw width
                "centerYPx": 100,
                "reviewed": True,
            }
        },
    )
    check_raises(
        lambda: collect_reviewed_samples(rec_dir),
        RecordingValidationError,
        "center outside the recording's own raw frame must raise",
    )


def test_absent_with_center_raises(root: Path) -> None:
    rec_dir = make_fixture_recording(
        root,
        "rec_absent_with_center",
        labels={"0": {"presence": "absent", "centerXPx": 10, "centerYPx": 10, "reviewed": True}},
    )
    check_raises(
        lambda: collect_reviewed_samples(rec_dir),
        RecordingValidationError,
        "presence=absent with a non-null center must raise",
    )


def test_missing_referenced_frame_raises(root: Path) -> None:
    rec_dir = make_fixture_recording(
        root,
        "rec_missing_frame_file",
        n_frames=3,
        skip_frame_index=1,
        labels={"1": {"presence": "absent", "centerXPx": None, "centerYPx": None, "reviewed": True}},
    )
    check_raises(
        lambda: collect_reviewed_samples(rec_dir),
        RecordingValidationError,
        "a label referencing a frame file that does not exist must raise",
    )


def test_ambiguous_and_unreviewed_are_excluded(root: Path) -> None:
    rec_dir = make_fixture_recording(
        root,
        "rec_exclusions",
        n_frames=4,
        labels={
            "0": {"presence": "present", "centerXPx": 640, "centerYPx": 360, "reviewed": True},
            "1": {"presence": "ambiguous", "centerXPx": None, "centerYPx": None, "reviewed": True},
            # frame 2 intentionally has NO entry at all -- never reviewed.
            "3": {"presence": "absent", "centerXPx": None, "centerYPx": None, "reviewed": True},
        },
    )
    samples = collect_reviewed_samples(rec_dir)
    frame_indices = sorted(s.frame_index for s in samples)
    check(frame_indices == [0, 3], f"expected only frames [0, 3] to survive exclusion, got {frame_indices}")


def test_dataset_getitem_shapes_and_coordinate_transform(root: Path) -> None:
    # A known raw-space center, transformed by the SAME resize ratio the
    # RealBeaconDataset applies (FIXTURE_RAW_W/H -> ORIG_W/H), must decode back
    # out (via the frozen heatmap encode/decode round trip) close to the
    # expected ORIG-space coordinate -- this is the concrete "labels move with
    # the image" check, not just a shape check.
    raw_x, raw_y = FIXTURE_RAW_W * 0.75, FIXTURE_RAW_H * 0.25
    expected_orig_x = raw_x * (ORIG_W / FIXTURE_RAW_W)
    expected_orig_y = raw_y * (ORIG_H / FIXTURE_RAW_H)

    rec_dir = make_fixture_recording(
        root,
        "rec_transform_check",
        n_frames=2,
        labels={
            "0": {"presence": "present", "centerXPx": raw_x, "centerYPx": raw_y, "reviewed": True},
            "1": {"presence": "full_occlusion", "centerXPx": None, "centerYPx": None, "reviewed": True},
        },
    )
    ds = RealBeaconDataset(root, [rec_dir.name])
    check(len(ds) == 2, f"expected 2 samples, got {len(ds)}")

    by_frame = {int(sid.split(":")[1]): i for i, sid in enumerate(ds.sample_ids())}

    x0, hm0, present0, xy0, _ = ds[by_frame[0]]
    check(tuple(x0.shape) == (1, 240, 320), f"positive sample input shape wrong: {tuple(x0.shape)}")
    check(tuple(hm0.shape) == (60, 80), f"positive sample heatmap shape wrong: {tuple(hm0.shape)}")
    check(float(present0) == 1.0, "positive sample present must be 1.0")

    # decode_heatmap()'s soft-argmax over a clamped window is a known-approximate
    # inverse of gaussian_heatmap() even for a perfect analytic Gaussian (its
    # window can clip tail mass near a cell boundary) -- the model card's own
    # measured centroid error is ~1.7 px median / up to ~10 px on trained-network
    # heatmaps, so a several-px gap here is the expected shape of that existing,
    # frozen approximation, not evidence this module's transform is wrong. The
    # tight check on the untouched-by-decode `xy0` below is the real assertion;
    # this one guards against a grosser mistake (e.g. x/y swapped, or a
    # heatmap-space coordinate passed where an orig-space one was expected).
    decoded_x, decoded_y, _ = decode_heatmap(hm0.numpy())
    check(
        abs(decoded_x - expected_orig_x) < 8.0 and abs(decoded_y - expected_orig_y) < 8.0,
        f"heatmap peak decodes to ({decoded_x:.2f}, {decoded_y:.2f}), "
        f"expected roughly ({expected_orig_x:.2f}, {expected_orig_y:.2f}) -- "
        "the raw->orig label transform looks grossly wrong",
    )
    check(
        abs(float(xy0[0]) - expected_orig_x) < 1e-3 and abs(float(xy0[1]) - expected_orig_y) < 1e-3,
        "returned label_xy does not match the expected raw->orig transform",
    )

    x1, hm1, present1, xy1, _ = ds[by_frame[1]]
    check(float(present1) == 0.0, "negative (full_occlusion) sample present must be 0.0")
    check(float(hm1.sum()) == 0.0, "negative sample heatmap must be all-zero")
    check(tuple(xy1.tolist()) == (-1.0, -1.0), "negative sample label_xy must be (-1, -1)")


def test_split_no_leakage_and_determinism(root: Path) -> None:
    ids = [f"group_{i}" for i in range(10)]
    a = assign_splits(ids, seed=1)
    b = assign_splits(ids, seed=1)
    check(a == b, "same seed must produce the same assignment")

    counts = {"train": 0, "val": 0, "test": 0}
    seen: set[str] = set()
    for rid, split in a.items():
        check(rid not in seen, f"{rid} assigned more than once")
        seen.add(rid)
        counts[split] += 1
    check(seen == set(ids), "every recording id must appear in exactly one split")
    check(counts["train"] == 6 and counts["val"] == 2 and counts["test"] == 2, f"unexpected split sizes: {counts}")

    c = assign_splits(ids, seed=2)
    check(a != c, "a different seed should (with overwhelming probability) change the assignment")


def test_split_too_few_groups_raises() -> None:
    check_raises(
        lambda: assign_splits(["only_one"]),
        ValueError,
        "splitting 1 group into train/val/test (3 non-empty splits) must raise, not silently leave splits empty",
    )


def test_split_manifest_round_trip(root: Path) -> None:
    ids = [f"g{i}" for i in range(5)]
    assignment = assign_splits(ids, seed=7)
    path = root / "split.json"
    save_split_manifest(path, assignment, note="fixture")
    loaded = load_split_manifest(path)
    check(loaded == assignment, "save/load split manifest round trip must be exact")


def test_discover_recording_ids_ignores_dirs_without_manifest(root: Path) -> None:
    discover_root = root / "discover"
    make_fixture_recording(discover_root, "has_manifest")
    (discover_root / "stray_dir").mkdir(parents=True, exist_ok=True)
    (discover_root / "stray_file.txt").write_text("not a recording")
    ids = discover_recording_ids(discover_root)
    check(ids == ["has_manifest"], f"expected only ['has_manifest'], got {ids}")


def main() -> int:
    if cv2 is None:
        print("SKIP: opencv-python not installed")
        return 0

    root = Path(tempfile.mkdtemp(prefix="fsoc_real_dataset_tests_"))
    try:
        test_missing_manifest_raises(root)
        test_missing_labels_raises(root)
        test_wrong_coordinate_space_raises(root)
        test_present_without_center_raises(root)
        test_center_outside_raw_bounds_raises(root)
        test_absent_with_center_raises(root)
        test_missing_referenced_frame_raises(root)
        test_ambiguous_and_unreviewed_are_excluded(root)
        test_dataset_getitem_shapes_and_coordinate_transform(root)
        test_split_no_leakage_and_determinism(root)
        test_split_too_few_groups_raises()
        test_split_manifest_round_trip(root)
        test_discover_recording_ids_ignores_dirs_without_manifest(root)
    finally:
        shutil.rmtree(root, ignore_errors=True)

    if failures == 0:
        print("PASS: all real_dataset/real_dataset_split checks passed.")
        return 0
    print(f"FAILED: {failures} check(s).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
