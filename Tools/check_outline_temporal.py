"""Bounded acceptance gates for the recorded temporal experiment, not all UE scenes."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "Documentation/Outline/Temporal"
metrics = json.loads((BASE / "TemporalMetrics.json").read_text(encoding="utf-8"))
stage = "Candidate1"
results = json.loads((BASE / "Validation" / stage / "Results.json").read_text(encoding="utf-8"))
values = metrics["stages"][stage]
checks = []

def check(name, passed, detail=None):
    checks.append({"name": name, "passed": bool(passed), "detail": detail})

check("AllCasesCaptured", results["status"] == "Captured" and len(results["cases"]) == 13)
for case in results["cases"]:
    name = case["name"]
    frames = [f for f in results["frames"] if f["case"] == name]
    states = [f["preview_state"] for f in frames]
    check(name + ":32RealFrames", len(frames) == values[name]["frames"] == 32)
    check(name + ":CorrectAA", all(int(re.search(r"AA=(\d+)", s)[1]) == case["AA"] for s in states))
    view_counts = [int(re.search(r"Views=(\d+)", s)[1]) for s in states]
    check(name + ":ViewportKeepsRendering", all(b > a for a, b in zip(view_counts, view_counts[1:])))
    helpers = [s.split("Helpers=", 1)[1] for s in states]
    check(name + ":StablePreviewHelpers", len(set(helpers)) == 1 and bool(helpers[0]))
for name in ("Screen_Static100", "Screen_Static66", "Screen_TAA100", "Screen_Width1", "Screen_Width8", "Black_Static100"):
    score = values[name]["relative_temporal_mae"]
    check(name + ":StaticVariationBelow3Percent", score < .03, score)
for name in ("Screen_Motion100", "Screen_Motion66", "Black_Motion100"):
    score = values[name]["motion_compensated_relative_mae"]
    check(name + ":MotionVariationBelow6Percent", score < .06, score)
static_reduction = 1 - values["Screen_Static100"]["relative_temporal_mae"] / values["Legacy_Static100"]["relative_temporal_mae"]
motion_reduction = 1 - values["Screen_Motion100"]["motion_compensated_relative_mae"] / values["Legacy_Motion100"]["motion_compensated_relative_mae"]
check("StaticFlickerReductionAbove90Percent", static_reduction > .9, static_reduction)
check("MotionFlickerReductionAbove50Percent", motion_reduction > .5, motion_reduction)
check("HullStableWithoutScreenPass", values["Hull_Static100"]["relative_temporal_mae"] < .04)
check("WidthResponds", values["Screen_Width8"]["red_mean_area"] > values["Screen_Width1"]["red_mean_area"] * 2)
check("DisplayWidthStableAt66Percent", abs(values["Screen_Static66"]["red_mean_area"] / values["Screen_Static100"]["red_mean_area"] - 1) < .1)
report = {"stage": stage, "passed": all(c["passed"] for c in checks), "checks": checks,
          "static_reduction": static_reduction, "motion_reduction": motion_reduction,
          "scope": "This character, camera, preview renderer, tested AA modes and resolutions only."}
(BASE / "Acceptance.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({"passed": report["passed"], "checks": len(checks), "failed": [c for c in checks if not c["passed"]],
                  "static_reduction": static_reduction, "motion_reduction": motion_reduction}, indent=2))
assert report["passed"]
