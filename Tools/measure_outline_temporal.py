"""Offline temporal metrics from actual viewport PNGs (numpy/Pillow).
No image smoothing is applied. Red excess isolates the diagnostic outline.
"""
import json
from pathlib import Path
import numpy as np
from PIL import Image

def shift_x(data, displacement):
    """Bilinear translation by less than one pixel along axis 2 (width)."""
    return data * (1 - abs(displacement)) + np.roll(data, 1 if displacement > 0 else -1, axis=2) * abs(displacement)

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "Documentation/Outline/Temporal/Validation"
report = {"metric": "Per-frame red excess on the union of outline pixels; bottom-left viewport axes excluded.",
          "stages": {}}
for stage in BASE.iterdir():
    if not stage.is_dir():
        continue
    cases = {}
    for case in sorted(stage.iterdir()):
        if not case.is_dir():
            continue
        paths = sorted(case.glob("*/Outline.png"))
        if len(paths) < 2:
            continue
        frames = np.stack([np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255 for p in paths])
        original = np.stack([np.asarray(Image.open(p.with_name("Original.png")).convert("RGB"), dtype=np.float32) / 255 for p in paths])
        black = case.name.startswith("Black")
        strength = (np.maximum(original.mean(axis=-1) - frames.mean(axis=-1), 0) if black else
                    np.maximum(frames[..., 0] - np.maximum(frames[..., 1], frames[..., 2]), 0))
        strength[:, -100:, :100] = 0
        region = np.max(strength, axis=0) > 0.15
        delta = np.abs(np.diff(strength, axis=0))
        scale = max(float(np.percentile(strength[:, region], 95)), .01) if region.any() else 1
        # Camera translation is .35 final pixels per sampled frame. Verify sign by minimizing
        # error on the original view; use the same displacement for its outlined counterpart.
        moving = "Motion" in case.name
        motion_delta = delta
        sign = 0
        if moving:
            candidates = []
            for direction in (-1, 1):
                aligned_original = shift_x(original[:-1], direction * .35)
                candidates.append((float(np.abs(original[1:] - aligned_original).mean()), direction))
            sign = min(candidates)[1]
            aligned = shift_x(strength[:-1], sign * .35)
            motion_delta = np.abs(strength[1:] - aligned)
        cases[case.name] = {
            "signal": "source_minus_outline_luminance" if black else "red_excess",
            "frames": len(paths), "outline_union_pixels": int(region.sum()),
            "red_mean_area": float((strength > .15).sum(axis=(1, 2)).mean()),
            "outline_temporal_mae_255": float(delta[:, region].mean() * 255) if region.any() else None,
            "outline_temporal_std_255": float(strength[:, region].std(axis=0).mean() * 255) if region.any() else None,
            "outline_changed_fraction_gt10": float((delta[:, region] > 10/255).mean()) if region.any() else None,
            "red_strength_p95": scale,
            "relative_temporal_mae": float(delta[:, region].mean() / scale) if region.any() else None,
            "motion_compensated_relative_mae": float(motion_delta[:, region].mean() / scale) if region.any() else None,
            "motion_alignment_px": sign * .35,
            "original_rgb_temporal_mae_255": float(np.abs(np.diff(original, axis=0)).mean() * 255),
            "whole_rgb_temporal_mae_255": float(np.abs(np.diff(frames, axis=0)).mean() * 255),
        }
        # Native captured frames, no retouching, scaled only by the viewer.
        images = [Image.open(p).convert("RGB") for p in paths[:16]]
        images[0].save(stage / (case.name + ".gif"), save_all=True, append_images=images[1:], duration=100, loop=0)
    report["stages"][stage.name] = cases
target = BASE.parent / "TemporalMetrics.json"
target.write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
