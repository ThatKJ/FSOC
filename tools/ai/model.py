"""TinyBeaconNet — a lightweight fully-convolutional heatmap-localization network.

Design goals (see docs/19_AI_PERCEPTION_ARCHITECTURE.md):
  * < 1M trainable parameters (actual: ~50k — printed by train_beacon_net.py)
  * single-channel input, real-time on CPU
  * two heads: beacon-presence logit + beacon-location heatmap logits
  * plain, reviewable ops only: Conv / depthwise-separable Conv / BN / ReLU /
    global max pool / Linear. No YOLO, no ResNet/ViT/CLIP, no pretrained weights.

Input : [N, 1, 240, 320]   float32 in [0, 1]
Output: presence_logit [N, 1]   (raw logit; sigmoid applied outside the graph)
        heatmap_logit  [N, 1, 60, 80]   (raw logits)
"""

from __future__ import annotations

import torch
import torch.nn as nn

from common import HM_H, HM_W, IN_H, IN_W


def _conv_bn_relu(in_ch: int, out_ch: int, k: int = 3, stride: int = 1) -> nn.Sequential:
    pad = k // 2
    return nn.Sequential(
        nn.Conv2d(in_ch, out_ch, k, stride=stride, padding=pad, bias=False),
        nn.BatchNorm2d(out_ch),
        nn.ReLU(inplace=True),
    )


class DepthwiseSeparable(nn.Module):
    """DWConv(k, stride) -> BN -> ReLU -> PWConv(1x1) -> BN -> ReLU."""

    def __init__(self, in_ch: int, out_ch: int, stride: int = 1) -> None:
        super().__init__()
        self.dw = nn.Sequential(
            nn.Conv2d(in_ch, in_ch, 3, stride=stride, padding=1, groups=in_ch, bias=False),
            nn.BatchNorm2d(in_ch),
            nn.ReLU(inplace=True),
        )
        self.pw = nn.Sequential(
            nn.Conv2d(in_ch, out_ch, 1, bias=False),
            nn.BatchNorm2d(out_ch),
            nn.ReLU(inplace=True),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.pw(self.dw(x))


class TinyBeaconNet(nn.Module):
    def __init__(self, width: int = 16) -> None:
        super().__init__()
        c1, c2, c3, c4 = width, width * 2, width * 3, width * 4  # 16, 32, 48, 64

        self.stem = _conv_bn_relu(1, c1, k=3, stride=2)          # 240x320 -> 120x160
        self.block1 = DepthwiseSeparable(c1, c2, stride=2)        # 120x160 -> 60x80
        self.block2 = DepthwiseSeparable(c2, c3, stride=1)        # 60x80
        self.block3 = DepthwiseSeparable(c3, c4, stride=1)        # 60x80  (== HM_H x HM_W)

        self.heatmap_head = nn.Sequential(
            nn.Conv2d(c4, c2, 3, padding=1, bias=False),
            nn.BatchNorm2d(c2),
            nn.ReLU(inplace=True),
            nn.Conv2d(c2, 1, 1),                                  # heatmap logits
        )

        # Global MAX pool (not average): the beacon is a sparse point source that
        # occupies << 1 cell of the 60x80 trunk feature map. Global *average*
        # pooling dilutes those few bright cells across ~4800 and the presence
        # head cannot learn "is there a peak anywhere"; global *max* pooling keeps
        # that signal. (Stage-2 training diagnostic — see DECISIONS.md ADR-017.)
        self.presence_head = nn.Sequential(
            nn.AdaptiveMaxPool2d(1),
            nn.Flatten(),
            nn.Linear(c4, c2),
            nn.ReLU(inplace=True),
            nn.Linear(c2, 1),                                     # presence logit
        )

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        x = self.stem(x)
        x = self.block1(x)
        x = self.block2(x)
        feat = self.block3(x)
        heatmap_logit = self.heatmap_head(feat)                   # [N, 1, 60, 80]
        presence_logit = self.presence_head(feat)                 # [N, 1]
        return presence_logit, heatmap_logit


def build_model(width: int = 16) -> TinyBeaconNet:
    model = TinyBeaconNet(width=width)
    # sanity: output shapes match the frozen contract
    with torch.no_grad():
        p, h = model(torch.zeros(1, 1, IN_H, IN_W))
    assert p.shape == (1, 1), p.shape
    assert h.shape == (1, 1, HM_H, HM_W), h.shape
    return model
