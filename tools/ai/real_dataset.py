"""RealBeaconDataset — reads reviewed real-camera recordings for G2 real-data
training. Additive to `dataset.py`'s BeaconDataset: the synthetic path is
untouched and keeps training/evaluating exactly as it did before this module
existed. See docs/LIVE_REALDATA_TASK_STATE.md G2/G3.

Layout (produced by fsoc_live's RealSessionRecorder + the /mission/annotate tool):
    <real_sessions_root>/<recordingId>/manifest.json      raw dims, calibration, ...
    <real_sessions_root>/<recordingId>/frames/frame_<N>.jpg   RAW camera frame
    <real_sessions_root>/<recordingId>/telemetry.jsonl    per-frame telemetry (a
        detector SUGGESTION only -- never read as ground truth by this module)
    <real_sessions_root>/<recordingId>/labels.json        REVIEWED human labels
        (frontend/app/api/real-sessions/[recordingId]/labels/route.ts is the
        writer; this module only reads it)

Every raw frame is resized to the SAME native geometry the synthetic pipeline
assumes (common.ORIG_W x common.ORIG_H, 640x480) before common.preprocess() takes
it the rest of the way to the 320x240 network input -- this is the exact resize
LiveTrackingSession's own preprocessing performs on a real camera frame before
running the detector (include/fsoc/live_preprocessing.hpp), so training and
inference agree on what the network's coordinate space means for a real capture.
A label's raw-pixel center is scaled by the SAME per-recording ratio in
`_raw_to_orig()` -- that is the one place to change if cropping or mirroring are
ever added to the real-camera preprocessing pipeline.

Exclusion policy (explicit, not silent): a recorded frame contributes a training
sample ONLY if it has a REVIEWED label with presence in {"present", "absent",
"partial_occlusion", "full_occlusion"}. "ambiguous" and any frame missing from
labels.json (never reviewed) are excluded entirely -- never forced into either
training target.
"""

from __future__ import annotations

import json
from pathlib import Path

try:
    import cv2
except ImportError:  # pragma: no cover - cv2 only needed at train/eval time
    cv2 = None

import torch
from torch.utils.data import Dataset

from common import ORIG_H, ORIG_W, empty_heatmap, gaussian_heatmap, preprocess

POSITIVE_PRESENCE = {"present", "partial_occlusion"}
NEGATIVE_PRESENCE = {"absent", "full_occlusion"}
EXCLUDED_PRESENCE = {"ambiguous"}
KNOWN_PRESENCE = POSITIVE_PRESENCE | NEGATIVE_PRESENCE | EXCLUDED_PRESENCE


class RecordingValidationError(RuntimeError):
    """A recording directory is missing a required file, or a label is
    inconsistent with its own recording's geometry. Callers must treat this as a
    data problem to fix, never silently skip while still training on the rest."""


class RealBeaconSample:
    __slots__ = ("recording_id", "frame_index", "presence", "center_x_raw", "center_y_raw")

    def __init__(
        self,
        recording_id: str,
        frame_index: int,
        presence: str,
        center_x_raw: float | None,
        center_y_raw: float | None,
    ) -> None:
        self.recording_id = recording_id
        self.frame_index = frame_index
        self.presence = presence
        self.center_x_raw = center_x_raw
        self.center_y_raw = center_y_raw


def load_manifest(recording_dir: Path) -> dict:
    manifest_path = recording_dir / "manifest.json"
    if not manifest_path.is_file():
        raise RecordingValidationError(f"{recording_dir}: missing manifest.json")
    manifest = json.loads(manifest_path.read_text())
    for key in ("rawWidthPx", "rawHeightPx", "recordingId", "sessionId"):
        if key not in manifest:
            raise RecordingValidationError(f"{manifest_path}: missing required field {key!r}")
    if manifest["rawWidthPx"] <= 0 or manifest["rawHeightPx"] <= 0:
        raise RecordingValidationError(f"{manifest_path}: rawWidthPx/rawHeightPx must be positive")
    return manifest


def load_labels_doc(recording_dir: Path) -> dict:
    labels_path = recording_dir / "labels.json"
    if not labels_path.is_file():
        raise RecordingValidationError(
            f"{recording_dir}: missing labels.json -- review this recording in "
            "/mission/annotate before using it for training"
        )
    doc = json.loads(labels_path.read_text())
    if doc.get("labelCoordinateSpace") != "raw":
        raise RecordingValidationError(
            f"{labels_path}: labelCoordinateSpace must be 'raw', got {doc.get('labelCoordinateSpace')!r}"
        )
    return doc


def collect_reviewed_samples(recording_dir: Path) -> list[RealBeaconSample]:
    """Validates one recording directory and returns its reviewed, non-ambiguous
    samples. Raises RecordingValidationError on any structural problem -- this
    never silently skips a broken recording and keeps going."""
    manifest = load_manifest(recording_dir)
    labels_doc = load_labels_doc(recording_dir)
    recording_id = manifest["recordingId"]
    raw_w, raw_h = manifest["rawWidthPx"], manifest["rawHeightPx"]

    samples: list[RealBeaconSample] = []
    for frame_index_str, label in labels_doc.get("labels", {}).items():
        presence = label.get("presence")
        if presence not in KNOWN_PRESENCE:
            raise RecordingValidationError(
                f"{recording_dir}: frame {frame_index_str} has unknown presence {presence!r}"
            )
        if presence in EXCLUDED_PRESENCE:
            continue
        if not label.get("reviewed"):
            # Defensive: the annotation tool never writes reviewed=false, but this
            # module must not treat an unreviewed entry as ground truth either way.
            continue

        frame_index = int(frame_index_str)
        cx, cy = label.get("centerXPx"), label.get("centerYPx")
        if presence in POSITIVE_PRESENCE:
            if cx is None or cy is None:
                raise RecordingValidationError(
                    f"{recording_dir}: frame {frame_index} presence={presence!r} has no center"
                )
            if not (0.0 <= cx <= raw_w and 0.0 <= cy <= raw_h):
                raise RecordingValidationError(
                    f"{recording_dir}: frame {frame_index} center ({cx}, {cy}) is outside "
                    f"this recording's own raw frame ({raw_w}x{raw_h}) -- check for a stale "
                    "label saved against a different recording's geometry"
                )
        elif cx is not None or cy is not None:
            raise RecordingValidationError(
                f"{recording_dir}: frame {frame_index} presence={presence!r} must not have a center"
            )

        frame_path = recording_dir / "frames" / f"frame_{frame_index}.jpg"
        if not frame_path.is_file():
            raise RecordingValidationError(
                f"{recording_dir}: labels.json references frame {frame_index} but "
                f"{frame_path} does not exist"
            )
        samples.append(RealBeaconSample(recording_id, frame_index, presence, cx, cy))
    return samples


def _raw_to_orig(x_raw: float, y_raw: float, raw_w: int, raw_h: int) -> tuple[float, float]:
    """The one place a label's coordinates are transformed alongside the image --
    a plain uniform resize today, matching LivePreprocessConfig (no crop/mirror in
    the current real-camera pipeline). Update this in lockstep if that changes."""
    return x_raw * (ORIG_W / raw_w), y_raw * (ORIG_H / raw_h)


class RealBeaconDataset(Dataset):
    """Same __getitem__ contract as dataset.py's BeaconDataset (input [1,240,320],
    heatmap [60,80], present scalar, label_xy [2] in ORIG 640x480 space, difficulty
    scalar) so real and synthetic samples can be combined with
    torch.utils.data.ConcatDataset without special-casing either one."""

    def __init__(self, real_sessions_root: str | Path, recording_ids: list[str]) -> None:
        if cv2 is None:
            raise RuntimeError("opencv-python is required; pip install opencv-python")
        if not recording_ids:
            raise ValueError("recording_ids must not be empty")
        self.root = Path(real_sessions_root)
        self.samples: list[RealBeaconSample] = []
        self._raw_dims: dict[str, tuple[int, int]] = {}
        for recording_id in recording_ids:
            recording_dir = self.root / recording_id
            manifest = load_manifest(recording_dir)
            self._raw_dims[recording_id] = (manifest["rawWidthPx"], manifest["rawHeightPx"])
            self.samples.extend(collect_reviewed_samples(recording_dir))
        if not self.samples:
            raise RuntimeError(
                f"no reviewed, non-ambiguous samples found across {recording_ids} under {self.root}"
            )

    def __len__(self) -> int:
        return len(self.samples)

    def sample_ids(self) -> list[str]:
        return [f"{s.recording_id}:{s.frame_index}" for s in self.samples]

    def __getitem__(self, idx: int):
        s = self.samples[idx]
        raw_w, raw_h = self._raw_dims[s.recording_id]
        frame_path = self.root / s.recording_id / "frames" / f"frame_{s.frame_index}.jpg"
        raw_frame = cv2.imread(str(frame_path), cv2.IMREAD_GRAYSCALE)
        if raw_frame is None:
            raise FileNotFoundError(f"cannot read {frame_path}")
        orig_frame = cv2.resize(raw_frame, (ORIG_W, ORIG_H), interpolation=cv2.INTER_AREA)
        x = preprocess(orig_frame)[0]  # [1, 240, 320]

        if s.presence in POSITIVE_PRESENCE:
            xo, yo = _raw_to_orig(s.center_x_raw, s.center_y_raw, raw_w, raw_h)
            hm = gaussian_heatmap(xo, yo)
            present = 1.0
        else:
            xo, yo = -1.0, -1.0
            hm = empty_heatmap()
            present = 0.0

        return (
            torch.from_numpy(x),
            torch.from_numpy(hm),
            torch.tensor(present, dtype=torch.float32),
            torch.tensor([xo, yo], dtype=torch.float32),
            torch.tensor(0.0, dtype=torch.float32),  # difficulty: not meaningful for real data
        )
