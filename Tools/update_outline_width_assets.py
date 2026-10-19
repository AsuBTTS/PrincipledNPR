"""Rebuild owned width-control materials and adopt a restrained demo width.
Run once after the C++ plugin build; this is asset authoring, not a test runner.
"""
import json
import runpy
import traceback
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
output = root / "Saved/NPR/Outline/WidthAssetUpdate.json"
output.parent.mkdir(parents=True, exist_ok=True)
report = {"status": "Running", "screen_base_px": 1.25, "hull_base_cm": 0.1}
try:
    runpy.run_path(str(root / "Tools/create_outline_backend_assets.py"))
    lib = unreal.NPROutlineEditorLibrary
    error = lib.finish_backend_compilation()
    if error:
        raise RuntimeError(error)
    runpy.run_path(str(root / "Tools/open_outline_character_demo.py"))
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    demo = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == "Outline_TexturedCharacter")
    if "PrincipledNPR.MultiScope.v1" not in [str(tag) for tag in demo.tags]:
        raise RuntimeError("Refuse to change an unowned demonstration actor")
    group = demo.get_component_by_class(unreal.NPROutlineActorComponent)
    values = group.base_settings
    values.width_pixels = 1.25
    values.hull_width_cm = 0.1
    group.modify()
    group.base_settings = values
    if not group.refresh_outline():
        raise RuntimeError(group.get_last_error())
    preset_result = lib.save_preset("/Game/NPR/Outline/MultiScope/OP_TexturedCharacter", values)
    preset = preset_result[0] if isinstance(preset_result, tuple) else preset_result
    if preset is None:
        raise RuntimeError("Unable to save the owned demonstration preset")
    if group.apply_preset(preset) is None:
        raise RuntimeError(group.get_last_error())
    if lib.save_actor_outline(group) is None:
        raise RuntimeError("Unable to save demonstration actor outline")
    lib.open_editor()
    report["status"] = "AssetsCompiledAndSaved"
    report["actor"] = demo.get_path_name()
    report["preset"] = preset.get_path_name()
except Exception:
    report["status"] = "Failed"
    report["exception"] = traceback.format_exc()
    unreal.log_error(report["exception"])
    raise
finally:
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
