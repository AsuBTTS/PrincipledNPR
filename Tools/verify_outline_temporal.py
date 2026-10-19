"""Continuous real viewport captures. Run in an isolated editor, never save the map.
NPR_TEMPORAL_STAGE selects the output directory (Baseline / Fixed).
"""
import json
import os
import runpy
import traceback
import math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
STAGE = os.environ.get("NPR_TEMPORAL_STAGE", "Baseline")
OUT = ROOT / "Documentation/Outline/Temporal/Validation" / STAGE
OUT.mkdir(parents=True, exist_ok=True)
LIB = unreal.NPROutlineEditorLibrary
ACT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world
REPORT = {"stage": STAGE, "status": "Running", "frames": [], "cases": []}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def flush():
    (OUT / "Results.json").write_text(json.dumps(REPORT, indent=2), encoding="utf-8")

def console(text):
    unreal.SystemLibrary.execute_console_command(WORLD(), text)

try:
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if STAGE != "Baseline":
        legacy_path = "/Game/NPR/Outline/Temporal/M_OutlineScreen_LegacyBaseline"
        if not assets.does_asset_exist(legacy_path):
            legacy = assets.duplicate_asset("/PrincipledNPR/Outline/M_OutlineScreen", legacy_path)
            assert legacy
            assert legacy.get_editor_property("blendable_location") == unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING
            assets.set_metadata_tag(legacy, "NPROutlineTemporalFixture", "LegacyAfterTonemap.v1")
            assert assets.save_loaded_asset(legacy, False)
        legacy = assets.load_asset(legacy_path)
        assert assets.get_metadata_tag(legacy, "NPROutlineTemporalFixture") == "LegacyAfterTonemap.v1"
        runpy.run_path(str(ROOT / "Tools/create_outline_backend_assets.py"))
        assert LIB.finish_backend_compilation() == ""
    runpy.run_path(str(ROOT / "Tools/open_outline_character_demo.py"))
    actor = next(a for a in ACT.get_all_level_actors() if a.get_actor_label() == "Outline_TexturedCharacter")
    group = actor.get_component_by_class(unreal.NPROutlineActorComponent)
    LIB.set_comparison_capture_size(420, 560)
    # Saturated red separates the rendered outline from the character and background.
    original_settings = group.base_settings
    original_scope = group.scope
    screen_template = group.screen_template
    # name, method, jitter, AA, percentage, camera motion, legacy shader, width, black color
    screen, hull, none = unreal.NPROutlineMethod.SCREEN_SPACE, unreal.NPROutlineMethod.INVERTED_HULL, unreal.NPROutlineMethod.NONE
    cases = [("Screen_TSR_Jitter1", screen, 1, 4, 100, False, False, 3, False),
             ("Screen_TSR_Jitter0", screen, 0, 4, 100, False, False, 3, False),
             ("Hull_TSR_Jitter1", hull, 1, 4, 100, False, False, 3, False),
             ("None_TSR", none, 1, 4, 100, False, False, 3, False)]
    if STAGE != "Baseline":
        cases = [("Legacy_Static100", screen, 1, 4, 100, False, True, 3, False),
                 ("Screen_Static100", screen, 1, 4, 100, False, False, 3, False),
                 ("Screen_Static66", screen, 1, 4, 66, False, False, 3, False),
                 ("Screen_TAA100", screen, 1, 2, 100, False, False, 3, False),
                 ("Hull_Static100", hull, 1, 4, 100, False, False, 3, False),
                 ("Legacy_Motion100", screen, 1, 4, 100, True, True, 3, False),
                 ("Screen_Motion100", screen, 1, 4, 100, True, False, 3, False),
                 ("Screen_Motion66", screen, 1, 4, 66, True, False, 3, False),
                 ("Hull_Motion100", hull, 1, 4, 100, True, False, 3, False),
                 ("Screen_Width1", screen, 1, 4, 100, False, False, 1, False),
                 ("Screen_Width8", screen, 1, 4, 100, False, False, 8, False),
                 ("Black_Static100", screen, 1, 4, 100, False, False, 3, True),
                 ("Black_Motion100", screen, 1, 4, 100, True, False, 3, True)]
        camera_result = LIB.get_comparison_camera()
        base_location, base_rotation = camera_result
        REPORT["camera"] = {"location": [base_location.x, base_location.y, base_location.z],
                            "rotation": [base_rotation.pitch, base_rotation.yaw, base_rotation.roll]}
        # Camera looks along -Y. Moving X translates the silhouette at approximately .35 display px/frame.
        step_cm = abs(base_location.y) * 2 * math.tan(math.radians(22.5)) / 420 * .35
    case_index = -1
    frame = 0
    warmup = 90
    samples = 32
    helper_names = None

    def begin_case():
        global case_index, frame, helper_names
        case_index += 1
        frame = 0
        if case_index >= len(cases):
            return False
        name, method, jitter, aa, percentage, moving, old, width, black = cases[case_index]
        console("r.AntiAliasingMethod " + str(aa))
        console("r.CustomDepthTemporalAAJitter " + str(jitter))
        if STAGE != "Baseline":
            LIB.set_comparison_screen_percentage(percentage)
            LIB.set_comparison_camera(base_location, base_rotation)
            assert LIB.set_validation_screen_template(group, legacy if old else screen_template)
        values = unreal.NPROutlineSettings()
        values.method = method
        values.color = unreal.LinearColor(0.01, 0.01, 0.015, 1) if black else unreal.LinearColor(0.8, 0.01, 0.01, 1)
        values.width_pixels = width
        values.hull_width_cm = 0.3
        group.scope = (unreal.NPROutlineScope.INDEPENDENT_REGIONS if method == unreal.NPROutlineMethod.INVERTED_HULL
                       else unreal.NPROutlineScope.UNIFIED_SILHOUETTE)
        group.base_settings = values
        assert group.refresh_outline(), group.get_last_error()
        helper_names = [h.get_path_name() for h in group.get_outline_meshes()]
        REPORT["cases"].append({"name": name, "warmup_frames": warmup, "sample_frames": samples,
                                "initial_helpers": helper_names, "AA": aa, "screen_percentage": percentage,
                                "motion": moving, "legacy": old, "width_pixels": width})
        flush()
        return True

    begin_case()

    def tick(delta):
        global frame
        try:
            frame += 1
            assert helper_names == [h.get_path_name() for h in group.get_outline_meshes()], "Helpers rebuilt on unchanged frames"
            if frame <= warmup:
                return
            name = cases[case_index][0]
            index = frame - warmup - 1
            folder = OUT / name / ("%03d" % index)
            assert LIB.export_comparison(str(folder))
            state = LIB.get_comparison_temporal_state() if STAGE != "Baseline" else "UnavailableOnBaselineBuild"
            REPORT["frames"].append({"case": name, "index": index, "delta_seconds": delta, "preview_state": state})
            if STAGE != "Baseline" and cases[case_index][5]:
                LIB.set_comparison_camera(unreal.Vector(base_location.x + step_cm * (index + 1), base_location.y, base_location.z), base_rotation)
            if index + 1 == samples:
                flush()
                if not begin_case():
                    group.base_settings = original_settings
                    group.scope = original_scope
                    assert LIB.set_validation_screen_template(group, screen_template)
                    group.refresh_outline()
                    console("r.CustomDepthTemporalAAJitter 1")
                    REPORT["status"] = "Captured"
                    flush()
                    unreal.unregister_slate_post_tick_callback(callback)
                    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                    if STAGE != "Baseline":
                        runpy.run_path(str(ROOT / "Tools/run_outline_validation_session.py"))
        except Exception:
            REPORT["status"] = "Failed"
            REPORT["exception"] = traceback.format_exc()
            flush()
            unreal.log_error(REPORT["exception"])
            unreal.unregister_slate_post_tick_callback(callback)
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)

    callback = unreal.register_slate_post_tick_callback(tick)
except Exception:
    REPORT["status"] = "Failed"
    REPORT["exception"] = traceback.format_exc()
    flush()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise
