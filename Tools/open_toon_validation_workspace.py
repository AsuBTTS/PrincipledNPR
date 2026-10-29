"""Open a private validation-map copy for real UI checks; never save the source map."""
from datetime import datetime
from pathlib import Path
import json
import unreal

if "-ExecutePythonScript=" in unreal.SystemLibrary.get_command_line():
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
level_path = "/Game/NPR/ToonValidation/L_ToonWorkspace_" + datetime.now().strftime("%Y%m%d_%H%M%S")
if not unreal.EditorLoadingAndSavingUtils.save_map(world, level_path):
    raise RuntimeError("Unable to save a separate validation map")
world = unreal.EditorLoadingAndSavingUtils.load_map(level_path)
if world is None:
    raise RuntimeError("Unable to reopen the separate validation map")

candidates = [a for a in actors.get_all_level_actors()
              if a.get_components_by_class(unreal.SkeletalMeshComponent)]
target = next((a for a in candidates if a.get_actor_label() == "Outline_TexturedCharacter"),
              candidates[0] if candidates else None)
if target is None:
    raise RuntimeError("No skeletal character found in validation map")
actors.set_selected_level_actors([target])
unreal.NPRModulationEditorLibrary.open_workspace()
if not unreal.NPRModulationEditorLibrary.select_feature("Toon"):
    raise RuntimeError("Toon page did not open")
report = {
    "map": level_path,
    "actor": target.get_path_name(),
    "source_map_unchanged": "/Game/NPR/Maps/L_Outline_MultiScope_v1",
    "slots": [{"mesh": str(row.mesh.get_name()), "slot": row.slot_index,
               "name": str(row.slot_name)}
              for row in unreal.NPRModulationEditorLibrary.get_mesh_effects(target)],
}
out = Path(unreal.Paths.project_saved_dir()) / "ToonValidation" / "WorkspaceBoot.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("Toon validation workspace ready: " + level_path)
