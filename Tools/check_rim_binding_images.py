"""Measure real UE captures from NPR.Rim.RenderBinding; never synthesizes images."""
import hashlib
import json
import argparse
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--captures', type=Path, default=ROOT / "Documentation/RimLight/BindingValidation")
parser.add_argument('--automation-report', type=Path, default=ROOT / "Saved/Automation/RimBinding/index.json")
parser.add_argument('--output', type=Path)
args = parser.parse_args()
OUT = args.captures
names = ["Off", "Default8", "Wide25", "Hard25", "Soft25", "Red25", "Red25Double", "OffAgain"]
images = {name: np.asarray(Image.open(OUT / (name + ".png")).convert("RGB"), dtype=np.float32) for name in names}
checks = {"Eight512Captures": all(im.shape == (512, 512, 3) for im in images.values())}
off = images["Off"]
delta = {name: np.maximum(im - off, 0) for name, im in images.items()}
signal = {name: im.max(axis=2) for name, im in delta.items()}
area = {name: int((sig > 12).sum()) for name, sig in signal.items()}
checks["DefaultEnablesVisibleRim"] = area["Default8"] > 500
checks["WidthIncreasesCoverage"] = area["Wide25"] > 1.5 * area["Default8"]
transition = {}
for name in ["Hard25", "Soft25"]:
    s = signal[name]
    transition[name] = int(((s > 5) & (s < .85 * s.max())).sum())
checks["SoftnessBroadensTransition"] = transition["Soft25"] > 1.2 * transition["Hard25"]
red_mask = signal["Red25"] > 12
red_mean = delta["Red25"][red_mask].mean(axis=0) if red_mask.any() else np.zeros(3)
checks["RGBChangesHue"] = bool(red_mask.any() and red_mean[0] > 2 * max(red_mean[1], red_mean[2], 1))
red_gain = float((images["Red25Double"][..., 0] - images["Red25"][..., 0])[red_mask].mean()) if red_mask.any() else 0
checks["IntensityIncreasesRedContribution"] = red_gain > 3
off_error = float(np.abs(images["OffAgain"] - off).max())
checks["ZeroRestoresRenderedOff"] = off_error <= 2
automation = json.loads(args.automation_report.read_text(encoding="utf-8-sig"))
tests = [t for t in automation["tests"] if t["fullTestPath"].startswith("NPR.Rim.")]
required_tests = {"NPR.Rim.LiveBinding", "NPR.Rim.MaterialContract", "NPR.Rim.RenderBinding",
                  "NPR.Rim.StyleProfile", "NPR.Rim.ProfileBinding"}
checks["RequiredRimCppTestsSucceeded"] = required_tests.issubset({t["fullTestPath"] for t in tests}) and all(
    t["state"] == "Success" and t["errors"] == 0 for t in tests)
report = {
    "status": "Passed" if all(checks.values()) else "Failed",
    "checks": {k: bool(v) for k, v in checks.items()},
    "thresholds": {"signal_channel_delta_8bit": 12, "minimum_on_pixels": 500,
                   "wide_to_default_area_ratio_min": 1.5, "soft_to_hard_transition_ratio_min": 1.2,
                   "minimum_red_channel_gain_8bit": 3, "off_again_max_error_8bit": 2},
    "metrics": {"signal_area_pixels": area, "transition_pixels": transition,
                "red_mean_rgb_delta": red_mean.tolist(), "double_intensity_red_gain": red_gain,
                "off_again_max_error": off_error},
    "captures_sha256": {n + ".png": hashlib.sha256((OUT / (n + ".png")).read_bytes()).hexdigest() for n in names},
    "automation": {"reportCreatedOn": automation["reportCreatedOn"], "devices": [{k: d[k] for k in ["platform", "oSVersion", "gPU", "renderMode", "rHI"]} for d in automation["devices"]],
                   "tests": [{k: t[k] for k in ["fullTestPath", "state", "errors", "warnings", "duration"]} for t in tests]},
    "scope": "One stable MID in an isolated preview world, eight parameter variants, DX12/SM6. Not character, shadow, animation, temporal stability, Cook or performance acceptance. PNGs are real render-target exports, not UI screenshots.",
    "image_review": "See ImageReview.json; review applies only to matching image hashes"
}
output = args.output or OUT / "ImageMetrics.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(report["status"], json.dumps(report["checks"]))
assert all(checks.values()), "Real rendered Rim binding image gate failed; inspect ImageMetrics.json"
