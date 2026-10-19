"""Actual DX12 editor integration workflow; stages Create and Reopen run in separate processes.
NPR_OUTLINE_STAGE=Create|Reopen; NPR_OUTLINE_KEEP_OPEN=1 leaves the verified editor available.
Pixel evidence must also be inspected; this script does not equate readback checks with image acceptance.
"""
import json
import os
import runpy
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / "PrincipledNPR.uproject").is_file()
assert "nullrhi" not in unreal.SystemLibrary.get_command_line().lower(), "GPU evidence requires a real RHI"
STAGE = os.environ.get("NPR_OUTLINE_STAGE", "Create")
OUT = ROOT / os.environ.get("NPR_VALIDATION_OUTPUT", "Saved/NPR/Outline/Validation/" + STAGE)
OUT.mkdir(parents=True, exist_ok=True)
MAP = "/Game/NPR/Maps/L_Outline_Workflow"
PRESET = "/Game/NPR/Outline/Presets/OP_ControlledOutline"
REPORT = {"stage": STAGE, "status": "Running", "checks": [], "captures": [], "render_acceptance": "PendingImageInspection"}
lib = unreal.NPROutlineEditorLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def record(name, condition, detail=None):
    REPORT["checks"].append({"name": name, "passed": bool(condition), "detail": detail})
    flush()
    assert condition, name


def flush():
    (OUT / "Workflow.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")


def finish(failed=False):
    REPORT["status"] = "Failed" if failed else "DataAndWorkflowPassed_PixelReviewRequired"
    flush()
    if not os.environ.get("NPR_VALIDATION_SESSION") and (not os.environ.get("NPR_OUTLINE_KEEP_OPEN") or failed):
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def result_bool(result):
    # UE Python consumes a bool success return when UFUNCTION also has output parameters.
    # bool + FString& therefore yields the output string on success, None on failure.
    if isinstance(result, tuple):
        return result[0] if isinstance(result[0], bool) else True
    if isinstance(result, bool):
        return result
    return result is not None and result == ""


def make_settings(method, color=(0.8, 0.01, 0.01), px=3.0, cm=0.25):
    value = unreal.NPROutlineSettings()
    value.method = method
    value.color = unreal.LinearColor(*color, 1.0)
    value.width_pixels = px
    value.hull_width_cm = cm
    return value


def binding_for(mesh):
    return [x for x in mesh.get_owner().get_components_by_class(unreal.NPROutlineComponent)
            if x.target_mesh == mesh and x.material_slot_index == 2]


callback = None
steps = []
frame = 0
delay = 0


def capture(name):
    directory = OUT / name
    record("Export_" + name, lib.export_comparison(str(directory)))
    record("PNGFiles_" + name, all((directory / n).is_file() and (directory / n).stat().st_size > 1000
                                    for n in ("Original.png", "Outline.png")))
    REPORT["captures"].append(str(directory.relative_to(ROOT)).replace("\\", "/"))


def variation(name, settings):
    def change():
        binding.settings = settings
        record("Refresh_" + name, binding.refresh_outline(), binding.get_last_error())
        lib.open_editor()  # Same re-selection path as the tool's Reload button.
    steps.extend([(change, 16), (lambda: capture(name), 4)])


try:
    if STAGE == "Create":
        runpy.run_path(str(ROOT / "Tools/create_outline_backend_assets.py"), run_name="outline_backend")
        runpy.run_path(str(ROOT / "Tools/create_outline_workflow_fixture.py"), run_name="outline_fixture")
    else:
        record("LoadSavedMap", levels.load_level(MAP))
    fixture = [x for x in actors.get_all_level_actors() if "PrincipledNPR.OutlineFixture.v1" in [str(t) for t in x.tags]]
    record("OneControlledCharacter", len(fixture) == 1)
    actor = fixture[0]
    mesh = actor.get_component_by_class(unreal.SkeletalMeshComponent)
    actors.set_selected_level_actors([actor])
    if STAGE == "Create":
        # Reset only this explicitly tagged fixture so Create is repeatable.
        for previous in binding_for(mesh):
            record("ResetOwnedFixtureBinding", result_bool(lib.restore_source(previous)))
    original_materials = [mesh.get_material(i) for i in range(mesh.get_num_materials())]
    if STAGE == "Create":
        settings = make_settings(unreal.NPROutlineMethod.INVERTED_HULL)
        result = lib.convert_mesh(mesh, 2, settings)
        record("ConvertSelectedSlot", result.success, result.error)
        binding = result.binding
        again = lib.convert_mesh(mesh, 2, settings)
        record("IdempotentConversion", again.success and again.binding == binding)
        record("OriginalMaterialRetained", binding.original_material == original_materials[2])
        record("ChildMICParentPreserved", binding.converted_material.get_editor_property("parent") == binding.original_material)
        for parameter in ("BaseColorTexture", "NormalTexture"):
            original = unreal.MaterialEditingLibrary.get_material_default_texture_parameter_value(binding.original_material, parameter)
            inherited = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(binding.converted_material, parameter)
            record("Inherited_" + parameter, original is not None and original == inherited)
        record("OnlySelectedSlotConverted", all(mesh.get_material(i) == original_materials[i]
                                                 for i in range(mesh.get_num_materials()) if i != 2))
        before = mesh.get_material(2)
        invalid = lib.convert_mesh(mesh, -1, settings)
        record("InvalidSlotRejectedWithoutMutation", not invalid.success and mesh.get_material(2) == before)
    else:
        found = binding_for(mesh)
        record("SavedBindingReconstructed", len(found) == 1)
        binding = found[0]
        preset = assets.load_asset(PRESET)
        record("SavedPresetLoads", isinstance(preset, unreal.NPROutlinePreset))
        record("SavedMethod", binding.settings.method == preset.settings.method == unreal.NPROutlineMethod.SCREEN_SPACE)
        record("SavedWidth", abs(binding.settings.width_pixels - 3.0) < 0.0001)
        record("SavedColor", all(abs(getattr(binding.settings.color, channel) - expected) < 0.0001
                                   for channel, expected in (("r", 0.01), ("g", 0.01), ("b", 0.015), ("a", 1.0))))
        record("SavedPresetReference", binding.preset == preset)
        record("SavedMaterialAndSource", mesh.get_material(2) == binding.converted_material
               and binding.converted_material.get_editor_property("parent") == binding.original_material)
        record("RebuildHelpersAfterReopen", binding.refresh_outline(), binding.get_last_error())
    errors = lib.finish_backend_compilation()
    record("ActualShaderCompilation", not errors, errors)
    lib.open_editor()
    record("FixedComparisonResolution", lib.set_comparison_capture_size(351, 493))

    if STAGE == "Create":
        variation("None", make_settings(unreal.NPROutlineMethod.NONE))
        variation("HullThin", make_settings(unreal.NPROutlineMethod.INVERTED_HULL, cm=0.12))
        variation("HullWide", make_settings(unreal.NPROutlineMethod.INVERTED_HULL, cm=0.5))
        variation("ScreenThin", make_settings(unreal.NPROutlineMethod.SCREEN_SPACE, px=2))
        variation("ScreenWide", make_settings(unreal.NPROutlineMethod.SCREEN_SPACE, px=6))
        variation("ScreenGreen", make_settings(unreal.NPROutlineMethod.SCREEN_SPACE, color=(0.01, 0.8, 0.01), px=6))

        def save_final():
            settings = make_settings(unreal.NPROutlineMethod.SCREEN_SPACE, color=(0.01, 0.01, 0.015), px=3)
            saved = lib.save_preset(PRESET, settings)
            preset = saved[0] if isinstance(saved, tuple) else saved
            record("SavePreset", preset is not None, str(saved))
            applied = lib.apply_preset(binding, preset)
            record("ApplyPreset", result_bool(applied), repr(applied))
            saved_binding = lib.save_binding(binding)
            record("SaveLevelAndMaterial", result_bool(saved_binding), repr(saved_binding))
            lib.open_editor()
        steps.append((save_final, 20))
        steps.append((lambda: capture("FinalSaved"), 4))
    else:
        steps.append((lambda: record("HelperExistsAfterTicks", binding.get_outline_mesh() is not None), 4))
        steps.append((lambda: capture("Reopened"), 4))

    def on_tick(delta):
        global frame, delay
        try:
            frame += 1
            if frame < 25:
                return
            if delay > 0:
                delay -= 1
                return
            if steps:
                action, delay = steps.pop(0)
                action()
            else:
                unreal.unregister_slate_post_tick_callback(callback)
                finish()
        except Exception:
            REPORT["exception"] = traceback.format_exc()
            unreal.unregister_slate_post_tick_callback(callback)
            finish(True)
            unreal.log_error(REPORT["exception"])
    callback = unreal.register_slate_post_tick_callback(on_tick)
except Exception:
    REPORT["exception"] = traceback.format_exc()
    finish(True)
    raise
