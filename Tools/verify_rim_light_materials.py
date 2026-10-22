"""Reopen Rim assets and capture real GPU sphere variants in an unsaved world.

Run with UnrealEditor -ExecutePythonScript, not -run=pythonscript / -NullRHI.
Does not save maps, material instances, imported meshes or existing actor bindings.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / "PrincipledNPR.uproject").is_file()
OUT = ROOT / "Documentation/RimLight/Validation"
OUT.mkdir(parents=True, exist_ok=True)
EDIT = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/PrincipledNPR"], True)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
REPORT = {"engine": unreal.SystemLibrary.get_engine_version(), "status": "Running",
          "checks": {}, "captures": [], "image_review": "Pending",
          "scope": "Independent process asset reload; static orthographic sphere, four parameters. No character, shadow, temporal or Cook acceptance."}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def record():
    (OUT / "Results.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")


def check(name, value):
    REPORT["checks"][name] = bool(value)
    record()
    assert value, name


def finish(error=None):
    REPORT["status"] = "Failed" if error else "CompiledReopened_CapturesExported"
    if error:
        REPORT["error"] = error
    record()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


try:
    check("RealRHI", "nullrhi" not in unreal.SystemLibrary.get_command_line().lower())
    fn = ASSETS.load_asset("/PrincipledNPR/RimLight/MF_RimLight")
    mat = ASSETS.load_asset("/PrincipledNPR/RimLight/M_RimLight")
    preview = ASSETS.load_asset("/PrincipledNPR/RimLight/MI_RimLight_Preview")
    check("AssetsReopen", fn is not None and mat is not None and preview is not None)
    check("FourParameters", set(map(str, EDIT.get_scalar_parameter_names(mat))) == {"亮边宽度", "边缘柔和度", "边缘光强度"}
          and set(map(str, EDIT.get_vector_parameter_names(mat))) == {"边缘光颜色"})
    check("DefaultOff", EDIT.get_material_default_scalar_parameter_value(mat, "边缘光强度") == 0)
    check("PreviewEnabled", EDIT.get_material_instance_scalar_parameter_value(preview, "边缘光强度") == 1)
    expressions = EDIT.get_material_function_expressions(fn)
    check("NoCustomHLSL", not any(isinstance(n, unreal.MaterialExpressionCustom) for n in expressions))
    check("FunctionInterface", sum(isinstance(n, unreal.MaterialExpressionFunctionInput) for n in expressions) == 7
          and sum(isinstance(n, unreal.MaterialExpressionFunctionOutput) for n in expressions) == 2)
    # Existing library entry drains global asset/shader compilation, then flushes rendering.
    # Its Outline-specific return text is not used as Rim validation.
    unreal.NPROutlineEditorLibrary.finish_backend_compilation()
    stats = EDIT.get_statistics(preview)
    REPORT["pixel_instructions"] = stats.num_pixel_shader_instructions
    check("CompiledShaderPresent", stats.num_pixel_shader_instructions > 0)
    (OUT / "MaterialGraph.txt").write_text(unreal.NPROutlineEditorLibrary.audit_material(preview), encoding="utf-8")

    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    sphere = ACTORS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    mesh = sphere.get_component_by_class(unreal.StaticMeshComponent)
    mesh.set_static_mesh(ASSETS.load_asset("/Engine/BasicShapes/Sphere"))
    mid = mesh.create_dynamic_material_instance(0, mat)
    check("TransientMID", mid is not None)
    light = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 100), unreal.Rotator(pitch=-35, yaw=15, roll=0))
    light.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(2.0)
    cap_actor = ACTORS.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(-200, 0, 0), unreal.Rotator(pitch=0, yaw=0, roll=0))
    cap = cap_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
    cap.projection_type = unreal.CameraProjectionMode.ORTHOGRAPHIC
    cap.ortho_width = 150.0
    cap.capture_every_frame = False
    cap.capture_on_movement = False
    cap.always_persist_rendering_state = True
    cap.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    cap.show_flag_settings = [unreal.EngineShowFlagsSetting(show_flag_name="TemporalAA", enabled=False),
                              unreal.EngineShowFlagsSetting(show_flag_name="Bloom", enabled=False)]
    pp = cap.post_process_settings
    pp.override_auto_exposure_method = True
    pp.auto_exposure_method = unreal.AutoExposureMethod.AEM_MANUAL
    pp.override_auto_exposure_bias = True
    pp.auto_exposure_bias = 0
    pp.override_auto_exposure_apply_physical_camera_exposure = True
    pp.auto_exposure_apply_physical_camera_exposure = False
    cap.post_process_settings = pp
    rt = unreal.RenderingLibrary.create_render_target2d(world, 512, 512, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap.texture_target = rt

    variants = [("Off", 8, 2, 0, (1, 1, 1)), ("Default8", 8, 2, 1, (1, 1, 1)),
                ("Wide25", 25, 2, 1, (1, 1, 1)), ("Hard25", 25, 0, 1, (1, 1, 1)),
                ("Soft25", 25, 10, 1, (1, 1, 1)), ("Red25", 25, 2, 1, (1, 0, 0)),
                ("Red25Double", 25, 2, 2, (1, 0, 0))]
    stage = 0
    delay = 30
    current = None

    def tick(delta):
        global stage, delay, current
        try:
            cap.capture_scene()
            if delay:
                delay -= 1
                return
            if current:
                unreal.RenderingLibrary.export_render_target(world, rt, str(OUT), current + ".png")
                check("Capture_" + current, (OUT / (current + ".png")).is_file())
                REPORT["captures"].append(current + ".png")
                current = None
            if stage < len(variants):
                name, width, soft, peak, rgb = variants[stage]
                for label, value in [("亮边宽度", width), ("边缘柔和度", soft), ("边缘光强度", peak)]:
                    mid.set_scalar_parameter_value(label, value)
                mid.set_vector_parameter_value("边缘光颜色", unreal.LinearColor(*rgb, 1))
                current = name
                stage += 1
                delay = 12
            else:
                unreal.unregister_slate_post_tick_callback(handle)
                finish()
        except Exception:
            unreal.unregister_slate_post_tick_callback(handle)
            finish(traceback.format_exc())

    handle = unreal.register_slate_post_tick_callback(tick)
except Exception:
    finish(traceback.format_exc())
    raise
