"""Create owned Rim assets from UE material nodes; run inside the UE 5.8 editor.

Pure angular appearance function, ordinary Surface material and visible preview MIC.
Never assigns a material to an existing actor or saves a user map.
"""
import hashlib
import json
import math
import sys
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / "PrincipledNPR.uproject").is_file(), "Wrong project"
sys.path.insert(0, str(ROOT / "Tools"))
from rim_light_contract import canonical_values, load_contract
CONTRACT = load_contract()
DEFAULTS = canonical_values({})
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
EDIT = unreal.MaterialEditingLibrary
FOLDER = "/PrincipledNPR/RimLight"
OWNER = "PrincipledNPR.RimLight.Angular.v1"
TAG = "NPRRimOwner"
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/PrincipledNPR"], True)
REPORT = {"engine": unreal.SystemLibrary.get_engine_version(), "assets": [],
          "status": "Running", "render_validation": "NotRun",
          "runtime_resolver": "NotConnected", "policy": "UnshadowedEmissiveAppearance"}


def owned(name, cls, factory):
    path = FOLDER + "/" + name
    disk_path = ROOT / "Plugins/PrincipledNPR/Content/RimLight" / (name + ".uasset")
    if disk_path.is_file() or ASSETS.does_asset_exist(path):
        obj = unreal.load_asset(path)
        if not isinstance(obj, cls) or ASSETS.get_metadata_tag(obj, TAG) != OWNER:
            raise RuntimeError("Refuse to overwrite unowned asset: " + path)
        # Existing owned assets may contain artist edits. Reuse, do not rebuild.
        return obj, False
    obj = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, cls, factory())
    assert obj, path
    ASSETS.set_metadata_tag(obj, TAG, OWNER)
    return obj, True


def node(owner, cls, x, y, description="", **props):
    create = EDIT.create_material_expression_in_function if isinstance(owner, unreal.MaterialFunction) else EDIT.create_material_expression
    result = create(owner, cls, x, y)
    assert result
    if description:
        result.set_editor_property("desc", description)
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def link(src, dst, pin, output=""):
    assert EDIT.connect_material_expressions(src, output, dst, pin), (src.get_name(), dst.get_name(), pin)


def unary(owner, cls, src, x, y, **props):
    result = node(owner, cls, x, y, **props)
    link(src, result, "")
    return result


def binary(owner, cls, a, b, x, y, **props):
    result = node(owner, cls, x, y, **props)
    link(a, result, "A")
    link(b, result, "B")
    return result


def constant(owner, value, x, y):
    return node(owner, unreal.MaterialExpressionConstant, x, y, r=value)


def band(fn, value, center, half_width, y):
    """Report section 4.1: cubic band, w=max(art, .5*fwidth(x), 1e-5)."""
    dx = unary(fn, unreal.MaterialExpressionDDX, value, 0, y + 100)
    dy = unary(fn, unreal.MaterialExpressionDDY, value, 0, y + 230)
    ax = unary(fn, unreal.MaterialExpressionAbs, dx, 180, y + 100)
    ay = unary(fn, unreal.MaterialExpressionAbs, dy, 180, y + 230)
    fw = binary(fn, unreal.MaterialExpressionAdd, ax, ay, 360, y + 130)
    half = node(fn, unreal.MaterialExpressionMultiply, 530, y + 130, const_b=0.5)
    link(fw, half, "A")
    effective = binary(fn, unreal.MaterialExpressionMax, half_width, half, 710, y + 70)
    safe = node(fn, unreal.MaterialExpressionMax, 880, y + 70, const_b=1e-5)
    link(effective, safe, "A")
    delta = binary(fn, unreal.MaterialExpressionSubtract, value, center, 710, y - 90)
    shifted = binary(fn, unreal.MaterialExpressionAdd, delta, safe, 1060, y - 20)
    twice = node(fn, unreal.MaterialExpressionMultiply, 1060, y + 130, const_b=2.0)
    link(safe, twice, "A")
    ratio = binary(fn, unreal.MaterialExpressionDivide, shifted, twice, 1240, y)
    u = unary(fn, unreal.MaterialExpressionSaturate, ratio, 1420, y)
    square = binary(fn, unreal.MaterialExpressionMultiply, u, u, 1600, y)
    double = node(fn, unreal.MaterialExpressionMultiply, 1600, y + 130, const_b=2.0)
    link(u, double, "A")
    slope = node(fn, unreal.MaterialExpressionSubtract, 1780, y + 130, const_a=3.0)
    link(double, slope, "B")
    return binary(fn, unreal.MaterialExpressionMultiply, square, slope, 1960, y)


def function_input(fn, name, order, default, vector=False, preview=None):
    inp = node(fn, unreal.MaterialExpressionFunctionInput, -1400, order * 160,
               input_name=name, sort_priority=order,
               input_type=unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3 if vector else unreal.FunctionInputType.FUNCTION_INPUT_SCALAR,
               use_preview_value_as_default=True)
    if preview is None:
        preview = (node(fn, unreal.MaterialExpressionConstant3Vector, -1800, order * 160,
                        constant=unreal.LinearColor(*default[:3], 1)) if vector
                   else constant(fn, default[0], -1800, order * 160))
    link(preview, inp, "Preview")
    return inp


def save(obj):
    assert ASSETS.save_loaded_asset(obj, False), obj.get_path_name()
    REPORT["assets"].append(obj.get_path_name())


try:
    fn, new = owned("MF_RimLight", unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew)
    if new:
        fn.set_editor_property("description", "角度边缘光：亮边宽度/柔和度以弧度输入，RGB 颜色与强度相乘。仅像素阶段，不含灯光可见度。")
        fn.set_editor_property("expose_to_library", True)
        nr_default = node(fn, unreal.MaterialExpressionVertexNormalWS, -1800, 0)
        ng_default = node(fn, unreal.MaterialExpressionVertexNormalWS, -1800, 200)
        v_default = node(fn, unreal.MaterialExpressionCameraVectorWS, -1800, 400)
        nr = function_input(fn, "RimNormalWS", 0, (0, 0, 1, 0), True, nr_default)
        ng = function_input(fn, "GeometryNormalWS", 1, (0, 0, 1, 0), True, ng_default)
        view = function_input(fn, "ViewDirectionWS", 2, (0, 0, 1, 0), True, v_default)
        omega = function_input(fn, "RimGrazingAngleRad", 3, (DEFAULTS["RimGrazingAngleRad"], 0, 0, 0))
        softness = function_input(fn, "RimEdgeHalfWidthRad", 4, (DEFAULTS["RimEdgeHalfWidthRad"], 0, 0, 0))
        peak = function_input(fn, "RimPeakScale", 5, (DEFAULTS["RimPeakScale"], 0, 0, 0))
        color = function_input(fn, "RimColor", 6, (*DEFAULTS["RimColor"], 0), True)
        nr = unary(fn, unreal.MaterialExpressionNormalize, nr, -1100, 0)
        ng = unary(fn, unreal.MaterialExpressionNormalize, ng, -1100, 200)
        view = unary(fn, unreal.MaterialExpressionNormalize, view, -1100, 400)
        nv = binary(fn, unreal.MaterialExpressionDotProduct, nr, view, -860, 0)
        bounded = unary(fn, unreal.MaterialExpressionSaturate, nv, -640, 0)
        gamma = unary(fn, unreal.MaterialExpressionArcsine, bounded, -420, 0)
        transition = band(fn, gamma, omega, softness, 0)
        shape = unary(fn, unreal.MaterialExpressionOneMinus, transition, 2160, 0)
        front = binary(fn, unreal.MaterialExpressionDotProduct, ng, view, -420, 650)
        zero = constant(fn, 0, -420, 850)
        front_gate = band(fn, front, zero, zero, 850)
        mask = binary(fn, unreal.MaterialExpressionMultiply, shape, front_gate, 2400, 100)
        amplitude = binary(fn, unreal.MaterialExpressionMultiply, peak, color, 2400, 420)
        rgb = binary(fn, unreal.MaterialExpressionMultiply, mask, amplitude, 2620, 100)
        for i, (name, src) in enumerate([("RimColorContribution", rgb), ("RimMask", mask)]):
            out = node(fn, unreal.MaterialExpressionFunctionOutput, 2860, i * 220, output_name=name, sort_priority=i)
            link(src, out, "")
        EDIT.update_material_function(fn)
    save(fn)

    mat, new = owned("M_RimLight", unreal.Material, unreal.MaterialFactoryNew)
    if new:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        EDIT.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        call = node(mat, unreal.MaterialExpressionMaterialFunctionCall, 240, 0)
        assert call.set_material_function(fn)
        scalar_specs = [(p["material_parameter"], p["default"], p["ui_slider_range"][1], p["function_input"])
                        for p in CONTRACT["parameters"] if p["type"] == "scalar"]
        for i, (label, default, slider, pin) in enumerate(scalar_specs):
            param = node(mat, unreal.MaterialExpressionScalarParameter, -600, i * 200,
                         description="角度单位：度（硬域 0..90°）" if i < 2 else "场景线性倍率，0 关闭；支持有限非负值",
                         parameter_name=label, default_value=default, group="边缘光", sort_priority=i,
                         slider_min=0.0, slider_max=slider)
            src = param
            if i < 2:
                src = node(mat, unreal.MaterialExpressionMultiply, -200, i * 200,
                           description="度 → 弧度；保留 canonical ID 的弧度语义", const_b=math.pi / 180.0)
                link(param, src, "A")
            link(src, call, pin)
        color = node(mat, unreal.MaterialExpressionVectorParameter, -600, 660,
                     description="线性 RGB，Alpha 不参与计算。支持有限非负 HDR 颜色。",
                     parameter_name="边缘光颜色", default_value=unreal.LinearColor(*DEFAULTS["RimColor"], 1),
                     group="边缘光", sort_priority=3)
        link(color, call, "RimColor")
        base = node(mat, unreal.MaterialExpressionConstant3Vector, 240, -280, constant=unreal.LinearColor(0.08, 0.08, 0.08, 1))
        roughness = constant(mat, 0.5, 240, -120)
        assert EDIT.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        assert EDIT.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        assert EDIT.connect_material_property(call, "RimColorContribution", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = EDIT.recompile_material(mat)
    REPORT["compile_errors"] = list(errors or [])
    assert not errors, errors
    save(mat)

    preview, new = owned("MI_RimLight_Preview", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew)
    if new:
        EDIT.set_material_instance_parent(preview, mat)
        # UE 5.8.3's setter returns false even after updating; verify the value.
        EDIT.set_material_instance_scalar_parameter_value(preview, "边缘光强度", 1.0)
        assert EDIT.get_material_instance_scalar_parameter_value(preview, "边缘光强度") == 1.0, "Preview intensity readback failed"
    EDIT.update_material_instance(preview)
    save(preview)
    REPORT["readback"] = {name: EDIT.get_material_instance_scalar_parameter_value(preview, name)
                          for name in ["亮边宽度", "边缘柔和度", "边缘光强度"]}
    REPORT["source_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    REPORT["asset_sha256"] = {str(p.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in (ROOT / "Plugins/PrincipledNPR/Content/RimLight").glob("*.uasset")}
    REPORT["status"] = "AssetsSaved_CompileRequested"  # GPU compilation and images are a separate gate.
except Exception as error:
    REPORT["status"] = "Failed"
    REPORT["error"] = repr(error)
    raise
finally:
    output = ROOT / "Documentation/RimLight/MaterialCreation.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
