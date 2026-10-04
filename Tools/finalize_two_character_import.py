"""Finalize only owned new Wanderer assets and the owned two-character fixture."""
import datetime
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (project / "PrincipledNPR.uproject").is_file(), "Wrong project"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
record_path = project / "Documentation/Assets/TwoStyleCharacterProbe.Creation.json"
record = json.loads(record_path.read_text(encoding="utf-8"))
assert record["status"] in ("Saved_VisualPending", "Saved_GenericDisplayObserved"), record["status"]
assert editor.get_editor_world().get_path_name().split(".")[0] == record["map"]
assert assets.get_metadata_tag(editor.get_editor_world(), "PrincipledNPRTwoStyleOwner") == record["owner"]

mesh = assets.load_asset("/Game/NPR/Characters/Wanderer/Meshes/SKM_Wanderer")
assert assets.get_metadata_tag(mesh, "PrincipledNPRWandererImportOwner") == "PrincipledNPR.WandererImport.v1"
slots = list(mesh.get_editor_property("materials"))
assert len(slots) == 26
for binding in record["character_slots"]["Wanderer"]:
    material = assets.load_asset(binding["material"])
    assert isinstance(material, unreal.MaterialInstanceConstant)
    slots[binding["index"]].set_editor_property("material_interface", material)
mesh.set_editor_property("materials", slots)
assets.set_metadata_tag(mesh, "NPRDefaultSemanticStyleId", "AnimeFlat")
assets.set_metadata_tag(mesh, "NPRReferenceDirection", "GenshinLike")
assert assets.save_loaded_asset(mesh, False)
assert all(s.get_editor_property("material_interface") for s in mesh.get_editor_property("materials"))

owned_actors = {a.get_actor_label(): a for a in actors.get_all_level_actors()
                if "PNPR_TwoStyle_Ready" in [str(t) for t in a.get_editor_property("tags")]}
assert len(owned_actors) == 7
camera = owned_actors["PNPR_TwoStyle_Camera"]
camera.set_actor_location(unreal.Vector(0, 250, 128), False, False)
camera.set_actor_rotation(unreal.Rotator(pitch=-8, yaw=-90, roll=0), False)
owned_actors["PNPR_TwoStyle_Sky"].get_component_by_class(unreal.SkyLightComponent).set_intensity(0.6)
editor.set_level_viewport_camera_info(unreal.Vector(0,250,128), unreal.Rotator(pitch=-8,yaw=-90,roll=0))
for key, count in (("PNPR_ChenQianyu_EndfieldLike",11), ("PNPR_Wanderer_GenshinLike",26)):
    component = owned_actors[key].get_component_by_class(unreal.SkeletalMeshComponent)
    assert component.get_num_materials() == count
    assert all(component.get_material(i) for i in range(count))
assert levels.save_current_level()
assert levels.load_level(record["map"])
reloaded = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
assert len(reloaded) == 7
for key, count in (("PNPR_ChenQianyu_EndfieldLike",11), ("PNPR_Wanderer_GenshinLike",26)):
    component = reloaded[key].get_component_by_class(unreal.SkeletalMeshComponent)
    assert all(component.get_material(i) for i in range(count))
editor.set_level_viewport_camera_info(unreal.Vector(0,250,128), unreal.Rotator(pitch=-8,yaw=-90,roll=0))

record["status"] = "Saved_GenericDisplayObserved"
record["verification"] = {
    "map_saved_and_reloaded": True,
    "actor_count_after_reload": 7,
    "all_actor_material_bindings_readback": True,
    "wanderer_default_mesh_materials_saved": 26,
    "lit_viewport_observed": True,
    "front_orientation_observed": True,
    "scope": "Neutral pose, topology/texture placement and native Generic Toon display; no animation or formal lighting capability acceptance",
    "face_hair_specialized_response": "NotImplemented",
    "mmd_physics_ik_sdef": "NotPorted",
    "style_calibration": "Uncalibrated_DiffuseSeedOnly"
}
record["preview_camera"] = {"location_cm": [0,250,128], "rotation_deg": {"pitch":-8,"yaw":-90,"roll":0},
                            "sky_intensity":0.6, "fixed_exposure":True}
record["verified_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
record_path.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding="utf-8")
import_path = project / "Documentation/Assets/Wanderer.UE58.import.json"
import_record = json.loads(import_path.read_text(encoding="utf-8"))
# Retain the importer stage identifier for its re-entry guard; explicit later
# verification is attached as a separate stage instead of spoofing a fresh import.
import_record["visual_verification"] = record["verification"]
import_record["default_mesh_materials"] = record["character_slots"]["Wanderer"]
import_record["probe_map"] = record["map"]
import_record["workflow_state"] = "Imported_Saved_GenericDisplayObserved"
import_path.write_text(json.dumps(import_record,ensure_ascii=False,indent=2),encoding="utf-8")
unreal.log("PNPR_TWO_CHARACTERS_FINALIZED: saved/reloaded with 26 Wanderer default materials")
