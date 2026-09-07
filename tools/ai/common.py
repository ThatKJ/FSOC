"""TinyBeaconNet — frozen preprocessing, label, and heatmap-decode contract.

This module is the SINGLE SOURCE OF TRUTH for every numeric convention that the
C++ inference detector (`src/ai_beacon_detector.cpp`) must reproduce bit-for-bit
within tolerance. Do not change a constant or a formula here without updating the
C++ mirror, the parity fixture, and docs/19 + docs/20 in the same commit.

Coordinate convention (identical to the rest of the repo):
    origin = top-left of the image, +x = right, +y = down.

Pipeline:
    CV_8UC1 640x480  --preprocess-->  float32 [1,1,240,320] in [0,1]
        --TinyBeaconNet-->  presence_logit (scalar)  +  heatmap_logit [1,60,80]
        --decode-->  (x_px, y_px) in ORIGINAL 640x480 pixels  +  confidence
"""

from __future__ import annotations

import numpy as np

try:
    import cv2
except ImportError:  # pragma: no cover - cv2 only needed at train/export time
    cv2 = None

# --------------------------------------------------------------------------- #
# FROZEN geometry constants
# --------------------------------------------------------------------------- #
ORIG_W, ORIG_H = 640, 480          # native virtual-camera frame
IN_W, IN_H = 320, 240              # TinyBeaconNet input (native / 2)
HM_W, HM_H = 80, 60               # heatmap head resolution (input / 4, native / 8)
INPUT_STRIDE = ORIG_W // HM_W      # 8 original pixels per heatmap cell (== ORIG_H // HM_H)

HEATMAP_SIGMA_CELLS = 1.75         # Gaussian label std-dev, in heatmap cells
DECODE_WINDOW_RADIUS = 2           # soft-argmax uses a (2R+1)^2 window around the peak

# The model emits RAW LOGITS for both heads. sigmoid() is applied OUTSIDE the
# graph, identically in Python eval and in C++ — keeps the ONNX graph tiny and
# BCEWithLogits exact.
APPLY_SIGMOID_IN_GRAPH = False

# ONNX interface
ONNX_INPUT_NAME = "input"
ONNX_PRESENCE_OUTPUT = "presence_logit"
ONNX_HEATMAP_OUTPUT = "heatmap_logit"
ONNX_OPSET = 12


# --------------------------------------------------------------------------- #
# Coordinate mapping — pixel-centre aligned (matches renderer's cx = W/2.0)
# --------------------------------------------------------------------------- #
def orig_to_heatmap(x_orig: float, y_orig: float) -> tuple[float, float]:
    """Original-pixel coordinate -> continuous heatmap coordinate."""
    xh = (x_orig + 0.5) / INPUT_STRIDE - 0.5
    yh = (y_orig + 0.5) / INPUT_STRIDE - 0.5
    return xh, yh


def heatmap_to_orig(x_hm: float, y_hm: float) -> tuple[float, float]:
    """Continuous heatmap coordinate -> original-pixel coordinate."""
    xo = (x_hm + 0.5) * INPUT_STRIDE - 0.5
    yo = (y_hm + 0.5) * INPUT_STRIDE - 0.5
    return xo, yo


# --------------------------------------------------------------------------- #
# Preprocessing  (must match src/ai_beacon_detector.cpp exactly)
# --------------------------------------------------------------------------- #
def preprocess(frame_u8: np.ndarray) -> np.ndarray:
    """CV_8UC1 HxW uint8 -> float32 [1,1,IN_H,IN_W] in [0,1].

    Resize uses OpenCV INTER_AREA (area averaging) so the C++ path — which calls
    the identical OpenCV routine — is numerically equivalent.
    """
    if cv2 is None:
        raise RuntimeError("opencv-python is required for preprocess(); pip install opencv-python")
    if frame_u8.ndim != 2:
        raise ValueError(f"expected single-channel HxW frame, got shape {frame_u8.shape}")
    if frame_u8.dtype != np.uint8:
        raise ValueError(f"expected uint8 frame, got {frame_u8.dtype}")

    resized = cv2.resize(frame_u8, (IN_W, IN_H), interpolation=cv2.INTER_AREA)
    x = resized.astype(np.float32) / 255.0
    return x.reshape(1, 1, IN_H, IN_W)


# --------------------------------------------------------------------------- #
# Gaussian target heatmap
# --------------------------------------------------------------------------- #
def gaussian_heatmap(x_orig: float, y_orig: float) -> np.ndarray:
    """Return a [HM_H, HM_W] float32 heatmap, peak 1.0 at the beacon location.

    The centre may lie slightly outside the grid (partial edge clipping) — the
    Gaussian is simply truncated by the frame border.
    """
    xh, yh = orig_to_heatmap(x_orig, y_orig)
    ys = np.arange(HM_H, dtype=np.float32).reshape(HM_H, 1)
    xs = np.arange(HM_W, dtype=np.float32).reshape(1, HM_W)
    d2 = (xs - xh) ** 2 + (ys - yh) ** 2
    hm = np.exp(-d2 / (2.0 * HEATMAP_SIGMA_CELLS ** 2))
    hm[hm < 1e-4] = 0.0
    return hm.astype(np.float32)


def empty_heatmap() -> np.ndarray:
    return np.zeros((HM_H, HM_W), dtype=np.float32)


# --------------------------------------------------------------------------- #
# Decode  (must match src/ai_beacon_detector.cpp exactly)
# --------------------------------------------------------------------------- #
def sigmoid(x: np.ndarray | float) -> np.ndarray | float:
    return 1.0 / (1.0 + np.exp(-np.asarray(x, dtype=np.float64)))


def decode_heatmap(heatmap_logit: np.ndarray) -> tuple[float, float, float]:
    """[HM_H, HM_W] logits -> (x_px, y_px, peak_confidence) in ORIGINAL pixels.

    1. sigmoid the logits
    2. integer argmax -> peak cell
    3. soft-argmax (intensity-weighted centroid) over a (2R+1)^2 window clamped
       to the grid  -> sub-pixel heatmap coordinate
    4. map back to original 640x480 pixels
    peak_confidence is the sigmoid value at the argmax cell.
    """
    hm = np.asarray(heatmap_logit, dtype=np.float64).reshape(HM_H, HM_W)
    prob = sigmoid(hm)

    iy, ix = np.unravel_index(int(np.argmax(prob)), prob.shape)
    peak_conf = float(prob[iy, ix])

    r = DECODE_WINDOW_RADIUS
    y0, y1 = max(0, iy - r), min(HM_H - 1, iy + r)
    x0, x1 = max(0, ix - r), min(HM_W - 1, ix + r)
    win = prob[y0:y1 + 1, x0:x1 + 1]
    wsum = float(win.sum())
    if wsum <= 0.0:
        xh, yh = float(ix), float(iy)
    else:
        ys = np.arange(y0, y1 + 1, dtype=np.float64).reshape(-1, 1)
        xs = np.arange(x0, x1 + 1, dtype=np.float64).reshape(1, -1)
        xh = float((win * xs).sum() / wsum)
        yh = float((win * ys).sum() / wsum)

    xo, yo = heatmap_to_orig(xh, yh)
    return xo, yo, peak_conf


def parameter_count(model) -> int:
    return sum(p.numel() for p in model.parameters() if p.requires_grad)
