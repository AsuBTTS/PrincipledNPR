"""Typed Rim contract and unit conversion. No Unreal dependency or material writes."""
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_contract():
    return json.loads((ROOT / "Documentation/RimLight/MaterialParameters.v1.json").read_text(encoding="utf-8"))


def definitions():
    source = json.loads((ROOT / "Documentation/Schemas/NPRParameterContract.v1.json").read_text(encoding="utf-8"))
    return {item["id"]: item for item in source["parameterDefinitions"]}


def validate_value(identifier, value):
    definition = definitions().get(identifier)
    allowed = {p["semantic_id"] for p in load_contract()["parameters"]}
    if identifier not in allowed:
        raise ValueError("Unknown Rim semantic ID: " + str(identifier))
    rgb = definition.get("valueType") == "linear_rgb"
    if rgb and (not isinstance(value, (tuple, list)) or len(value) != 3):
        raise ValueError("RimColor requires exactly three linear RGB components")
    components = value if rgb else [value]
    lo, hi = definition["hardDomain"]
    for component in components:
        if isinstance(component, bool) or not isinstance(component, (int, float)) or not math.isfinite(component):
            raise ValueError(identifier + " requires finite numeric values")
        if (lo is not None and component < lo) or (hi is not None and component > hi):
            raise ValueError(identifier + " outside hard domain")
        try:
            finite_float = math.isfinite(struct.unpack("f", struct.pack("f", component))[0])
        except (OverflowError, struct.error):
            finite_float = False
        if not finite_float:
            raise ValueError(identifier + " cannot be represented as a finite material float")
    return list(value) if rgb else value


def canonical_values(values):
    """Fill missing defaults; explicit zero and black are never treated as missing."""
    contract = load_contract()
    allowed = {p["semantic_id"] for p in contract["parameters"]}
    if set(values) - allowed:
        raise ValueError("Unknown Rim IDs: " + repr(sorted(set(values) - allowed)))
    return {p["semantic_id"]: validate_value(p["semantic_id"], values.get(p["semantic_id"], p["canonical_default"]))
            for p in contract["parameters"]}


def to_material_values(values):
    """Canonical radians -> current material's degree scalars, with RGB unchanged."""
    resolved = canonical_values(values)
    return {p["material_parameter"]: math.degrees(resolved[p["semantic_id"]])
            if p.get("transform") == "degrees * pi / 180" else resolved[p["semantic_id"]]
            for p in load_contract()["parameters"]}
