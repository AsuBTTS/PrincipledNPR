"""Set the owned probe's camera/light after observing model orientation."""
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (project / "PrincipledNPR.uproject").is_file(), "Wrong project"
expected = "/Game/NPR/Maps/L_ChenQianyu_AssetProbe"
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
world = editor.get_editor_world()
if not world.get_path_name().startswith(expected + ".") or assets.get_metadata_tag(world, "PrincipledNPRCharacterProbeOwner") != "PrincipledNPR.CharacterAssetProbe.v1":
    raise RuntimeError("Wrong or unowned world")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for label, location, rotation in [
    ("PNPR_CharacterProbe_Camera", unreal.Vector(0, 280, 140), unreal.Rotator(pitch=-10, yaw=-90, roll=0)),
    ("PNPR_CharacterProbe_Key", unreal.Vector(0, 0, 300), unreal.Rotator(pitch=-35, yaw=-100, roll=0))]:
    found = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    if len(found) != 1:
        raise RuntimeError("Expected unique owned probe actor")
    found[0].set_actor_location_and_rotation(location, rotation, False, False)
editor.set_level_viewport_camera_info(unreal.Vector(0, 280, 140), unreal.Rotator(pitch=-10, yaw=-90, roll=0))
if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
    raise RuntimeError("Map save failed")
record = project / "Documentation/Assets/ChenQianyu.ProbeCreation.json"
data = json.loads(record.read_text(encoding="utf-8"))
data["camera"] = {"location_cm": [0, 280, 140], "pitch_yaw_roll_deg": [-10, -90, 0]}
data["key_light_pitch_yaw_roll_deg"] = [-35, -100, 0]
data["model_forward_component_axis"] = "+Y observed; head-bone local axes still uncalibrated"
record.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("PNPR_CHARACTER_FRONT_VIEW_SAVED")
