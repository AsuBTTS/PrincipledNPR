"""Cross-check the independent JSON contract, C++ declaration and adapter resource rules."""
import argparse
import json
import math
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path)
args = parser.parse_args()
contract = json.loads((root / "Documentation/Schemas/NPRParameterContract.v1.json").read_text(encoding="utf-8"))
backend = json.loads((root / "Documentation/ToonLight/ToonBackendContract.v1.json").read_text(encoding="utf-8"))
schema = json.loads((root / "Documentation/Schemas/NPRStyleProfile.schema.v1.json").read_text(encoding="utf-8"))
header = (root / "Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRToonParameters.h").read_text(encoding="utf-8")
adapter = (root / "Plugins/PrincipledNPR/Source/NPREditor/Private/NPRToonBackend.cpp").read_text(encoding="utf-8")
defs = {p["id"]: p for p in contract["parameterDefinitions"]}
checks = {}
for semantic_id in backend["semanticIds"]:
    declaration = re.search(r"UPROPERTY\(([^\n]+)\)\s*double\s+" + semantic_id + r"\s*=\s*([\d.]+);", header)
    assert declaration, "Missing or unparsed canonical declaration: " + semantic_id
    meta, initial = declaration.groups()
    definition = defs[semantic_id]
    checks[semantic_id + ".default"] = math.isclose(float(initial), definition["schemaDefault"], abs_tol=1e-15)
    for label, index in (("ClampMin", 0), ("ClampMax", 1)):
        expected = definition["hardDomain"][index]
        value = re.search(label + r'="([^"]+)"', meta)
        checks[semantic_id + "." + label] = (value is None if expected is None else
            value is not None and math.isclose(float(value[1]), expected, abs_tol=1e-15))
    checks[semantic_id + ".stable_id"] = ('TEXT("' + semantic_id + '")') in header
checks["legacy_model_inherits"] = schema["properties"]["shadingModel"]["default"] == "Inherit"
checks["shape_default"] = schema["properties"]["toonResponse"]["properties"]["shape"]["default"] == backend["diffuse"]["shapeDefault"] == "Hermite"
checks["native_sampling_float16_guard"] = "65504.0" in adapter and "65504.f" in adapter
checks["private_profile_guard"] = "P->HasAnyFlags(RF_Transient)" in adapter and "P->GetOutermost()!=GetTransientPackage()" in adapter
checks["no_custom_shader_added"] = not (root / "Plugins/PrincipledNPR/Shaders").exists()
assert all(checks.values()), {k: v for k, v in checks.items() if not v}
report = {"status": "PassedCrossSourceChecks", "checks": checks,
          "scope": "JSON and C++ declarations/resource guards only; not a build or render test."}
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"Toon contract cross-check PASS: {len(checks)} checks; rendering checked separately.")
