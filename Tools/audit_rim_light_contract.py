"""Read-only comparison of saved Rim assets against the frozen parameter contract."""
import hashlib
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / "PrincipledNPR.uproject").is_file()
sys.path.insert(0, str(ROOT / "Tools"))
from rim_light_contract import load_contract

C = load_contract()
E = unreal.MaterialEditingLibrary
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/PrincipledNPR"], True)
mat, fn, preview = [unreal.load_asset(C[key]) for key in ["material", "function", "preview_instance"]]
assert mat and fn and preview
REPORT = {"status": "Running", "checked_at_utc": datetime.now(timezone.utc).isoformat(), "engine": unreal.SystemLibrary.get_engine_version(), "contract_version": C["contractVersion"], "checks": {}, "saved_assets": False, "parameters": []}


def check(name, ok):
    REPORT["checks"][name] = bool(ok)
    assert ok, name


def close(a, b):
    return math.isclose(a, b, abs_tol=1e-6)


try:
    check("ScalarNamesAndTypes", set(map(str, E.get_scalar_parameter_names(mat))) == {p["material_parameter"] for p in C["parameters"] if p["type"] == "scalar"})
    check("VectorNamesAndTypes", set(map(str, E.get_vector_parameter_names(mat))) == {p["material_parameter"] for p in C["parameters"] if p["type"] == "linear_rgb"})
    expressions = E.get_material_expressions(mat)
    function_inputs = {str(n.get_editor_property("input_name")): n for n in E.get_material_function_expressions(fn) if isinstance(n, unreal.MaterialExpressionFunctionInput)}
    call = E.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    check("FunctionFeedsEmissive", isinstance(call, unreal.MaterialExpressionMaterialFunctionCall) and call.get_editor_property("material_function") == fn)
    # Both APIs retain pin order, including unconnected inputs. Check each named pin,
    # not just whether some degree-conversion node reaches the function call.
    call_inputs = dict(zip(map(str, E.get_material_expression_input_names(call)), E.get_inputs_for_material_expression(mat, call)))
    for p in C["parameters"]:
        label, identifier = p["material_parameter"], p["semantic_id"]
        rgb = p["type"] == "linear_rgb"
        if rgb:
            value = E.get_material_default_vector_parameter_value(mat, label)
            actual = [value.r, value.g, value.b]
            check(identifier + "Default", all(close(a, b) for a, b in zip(actual, p["default"])))
        else:
            actual = E.get_material_default_scalar_parameter_value(mat, label)
            check(identifier + "Default", close(actual, p["default"]))
        param_node = next(n for n in expressions if isinstance(n, unreal.MaterialExpressionVectorParameter if rgb else unreal.MaterialExpressionScalarParameter) and str(n.get_editor_property("parameter_name")) == label)
        check(identifier + "Group", str(param_node.get_editor_property("group")) == "边缘光")
        if not rgb:
            check(identifier + "Sliders", close(param_node.get_editor_property("slider_min"), p["ui_slider_range"][0]) and close(param_node.get_editor_property("slider_max"), p["ui_slider_range"][1]))
        inp = function_inputs[p["function_input"]]
        check(identifier + "FunctionType", inp.get_editor_property("input_type") == (unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3 if rgb else unreal.FunctionInputType.FUNCTION_INPUT_SCALAR))
        defaults = E.get_inputs_for_material_function_expression(fn, inp)
        check(identifier + "FunctionDefaultConnected", len(defaults) == 1 and inp.get_editor_property("use_preview_value_as_default"))
        if rgb:
            value = defaults[0].get_editor_property("constant")
            check(identifier + "FunctionDefault", all(close(a, b) for a, b in zip([value.r, value.g, value.b], p["canonical_default"])))
        else:
            check(identifier + "FunctionDefault", close(defaults[0].get_editor_property("r"), p["canonical_default"]))
        if p.get("transform"):
            convert = call_inputs.get(p["function_input"])
            check(identifier + "FunctionPinBinding", isinstance(convert, unreal.MaterialExpressionMultiply) and E.get_inputs_for_material_expression(mat, convert)[0] == param_node)
            convert_inputs = E.get_inputs_for_material_expression(mat, convert)
            check(identifier + "DegreeConversion", len(convert_inputs) == 2 and convert_inputs[1] is None and close(convert.get_editor_property("const_b"), math.pi / 180))
        else:
            check(identifier + "FunctionPinBinding", call_inputs.get(p["function_input"]) == param_node)
        REPORT["parameters"].append({"semantic_id": identifier, "material_name": label, "actual_default": actual, "function_input": p["function_input"]})
    check("PreviewOverrideSeparate", E.get_material_instance_scalar_parameter_value(preview, "边缘光强度") == 1 and E.get_material_default_scalar_parameter_value(mat, "边缘光强度") == 0)
    check("AlphaExcludedByVector3", function_inputs["RimColor"].get_editor_property("input_type") == unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3)
    REPORT["asset_sha256"] = {str(p.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT / "Plugins/PrincipledNPR/Content/RimLight").glob("*.uasset")}
    REPORT["status"] = "Passed"
except Exception as error:
    REPORT["status"] = "Failed"
    REPORT["error"] = repr(error)
    raise
finally:
    (ROOT / "Documentation/RimLight/ParameterExposureValidation.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
