"""Cross-check semantic JSON, mapping, C++ declarations, and compatibility boundaries."""
import json
import math
import re
from pathlib import Path
from rim_light_contract import ROOT, canonical_values, definitions, load_contract, to_material_values, validate_value

contract = load_contract()
defs = definitions()
header_path = ROOT / "Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRRimParameters.h"
header = header_path.read_text(encoding="utf-8")
assert contract["contractVersion"] == contract["version"] == 1
rows = contract["parameters"]
assert len(rows) == len({p["semantic_id"] for p in rows}) == len({p["material_parameter"] for p in rows}) == 4
for name, row in zip(["Width", "Softness", "Intensity", "Color"], rows):
    identifier = row["semantic_id"]
    assert re.search(rf'{name}Id\[\] = TEXT\("{identifier}"\)', header)
    assert re.search(rf'{name}MaterialName\[\] = TEXT\("{row["material_parameter"]}"\)', header)
    assert row["canonical_default"] == defs[identifier]["schemaDefault"]
    assert row["canonical_unit"] == defs[identifier]["unit"]
    assert row["canonical_hard_domain"] == defs[identifier]["hardDomain"]
    if name != "Color":
        default = float(re.search(rf'\b{identifier} = ([\d.eE+-]+)f?;', header)[1])
        assert math.isclose(default, row["canonical_default"], abs_tol=1e-12)
    else:
        assert "RimColor = FLinearColor::White" in header
assert header.count("UPROPERTY(") == 4
style_schema = json.loads((ROOT / "Documentation/Schemas/NPRStyleProfile.schema.v1.json").read_text(encoding="utf-8"))
rgb_schema = style_schema["$defs"]["patch"]["properties"]["RimColor"]["properties"]["value"]
assert rgb_schema["minItems"] == rgb_schema["maxItems"] == 3
assert rgb_schema["items"] == {"type": "number", "minimum": 0}

checks = 0
for identifier in [p["semantic_id"] for p in rows]:
    validate_value(identifier, defs[identifier]["schemaDefault"])
    checks += 1
defaults = to_material_values({})
assert math.isclose(defaults["亮边宽度"], 8) and math.isclose(defaults["边缘柔和度"], 2)
assert defaults["边缘光强度"] == 0 and defaults["边缘光颜色"] == [1, 1, 1]
assert canonical_values({"RimColor": [0, 0, 0], "RimPeakScale": 0})["RimColor"] == [0, 0, 0]
assert validate_value("RimColor", [4, 2, 0]) == [4, 2, 0]  # HDR, no hidden normalization.
for degrees in [0, 1, 8, 35, 90]:
    value = to_material_values({"RimGrazingAngleRad": math.radians(degrees)})["亮边宽度"]
    assert math.isclose(value, degrees, abs_tol=1e-10)
    checks += 1
invalid = [("RimColor", [1, 1]), ("RimColor", [1, 1, 1, 1]), ("RimColor", [True, 0, 0]),
           ("RimColor", [-1, 0, 0]), ("RimColor", [float("nan"), 0, 0]),
           ("RimColor", [float("inf"), 0, 0]), ("RimColor", [1e40, 0, 0]),
           ("RimPeakScale", -1), ("RimPeakScale", True), ("RimPeakScale", None),
           ("RimPeakScale", float("inf")), ("RimGrazingAngleRad", math.pi),
           ("RimEdgeHalfWidthRad", -1), ("Unknown", 1)]
for identifier, value in invalid:
    try:
        validate_value(identifier, value)
    except ValueError:
        checks += 1
    else:
        raise AssertionError((identifier, value))
output = {"status": "Passed", "checks": checks + 4, "scope": "JSON / C++ declarations / conversion / rejection / compatibility; binary asset exposure audited separately", "contract_version": 1}
(ROOT / "Documentation/RimLight/ContractValidation.json").write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("Rim contract PASS:", output["checks"], "checks; no MID, Style Profile or render acceptance claimed.")
