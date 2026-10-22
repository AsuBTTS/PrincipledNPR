"""Verify the reflected C++ binding in the open editor using transient objects only."""
import json
import math
import runpy
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / "PrincipledNPR.uproject").is_file()
REPORT = {"status": "Running", "engine": unreal.SystemLibrary.get_engine_version(), "checks": {},
          "scope": "Reflected production C++ calls on transient mesh/component/package; no level actors, config or assets saved."}
checks = REPORT["checks"]
binding = None


def check(name, value):
    checks[name] = bool(value)
    assert value, name


def success(value):
    # UE may return the native bool + out string as a tuple, or use bool as success.
    if isinstance(value, tuple):
        return bool(value[0])
    if isinstance(value, bool):
        return value
    return value is not None


try:
    REPORT["reflection"] = unreal.NPRStyleBindingComponent.set_rim_parameters.__doc__
    material = unreal.load_asset("/PrincipledNPR/RimLight/M_RimLight")
    mesh = unreal.new_object(unreal.StaticMeshComponent)
    mesh.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Sphere"))
    original = mesh.get_material(0)
    binding = unreal.new_object(unreal.NPRStyleBindingComponent)
    package = unreal.new_object(unreal.NPRBackendStylePackage)
    package.set_editor_property("backend_id", "IndependentRim")
    slot = unreal.NPRBackendMaterialSlot()
    slot.set_editor_property("slot_index", 0)
    slot.set_editor_property("material", material)
    package.set_editor_property("material_slots", [slot])
    check("ApplyPackage", success(binding.apply_package(mesh, package)))
    mid = mesh.get_material(0)
    check("MIDCreated", isinstance(mid, unreal.MaterialInstanceDynamic))
    values = unreal.NPRRimParameters()
    values.set_editor_property("rim_grazing_angle_rad", math.radians(25))
    values.set_editor_property("rim_edge_half_width_rad", math.radians(4))
    values.set_editor_property("rim_peak_scale", 2)
    values.set_editor_property("rim_color", unreal.LinearColor(4, .2, .1, 0))
    result = binding.set_rim_parameters(values)
    REPORT["success_return"] = repr(result)
    check("FourControlUpdate", success(result))
    actual = [mid.get_scalar_parameter_value(n) for n in ["亮边宽度", "边缘柔和度", "边缘光强度"]]
    check("DegreesAndIntensityReadback", all(math.isclose(a, b, abs_tol=1e-5) for a, b in zip(actual, [25, 4, 2])))
    color = mid.get_vector_parameter_value("边缘光颜色")
    check("HDRRGBReadback", all(math.isclose(a, b, abs_tol=1e-5) for a, b in zip([color.r,color.g,color.b,color.a], [4,.2,.1,1])))
    values.set_editor_property("rim_peak_scale", -1)
    result = binding.set_rim_parameters(values)
    REPORT["rejected_return"] = repr(result)
    check("InvalidInputRejected", not success(result))
    check("PreviousValueRetained", mid.get_scalar_parameter_value("边缘光强度") == 2)
    check("MIDIdentityRetained", mesh.get_material(0) == mid)
    binding.restore_materials()
    check("OriginalRestored", mesh.get_material(0) == original)
    runpy.run_path(str(ROOT / "Tools/open_rim_light_material.py"))
    check("PreviewRefreshDispatched", True)
    REPORT["status"] = "Passed"
except Exception as error:
    REPORT.update(status="Failed", error=repr(error))
    raise
finally:
    if binding:
        binding.restore_materials()
    (ROOT / "Documentation/RimLight/BindingValidation/PythonReadback.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
