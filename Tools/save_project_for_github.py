"""Save dirty project packages only, including UE-converted migrated content."""
import datetime
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (project / "PrincipledNPR.uproject").is_file(), "Wrong project"
assert Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == project
utils = unreal.EditorLoadingAndSavingUtils
dirty = list(utils.get_dirty_map_packages()) + list(utils.get_dirty_content_packages())
packages = [p for p in dirty if p.get_path_name().startswith(("/Game/", "/PrincipledNPR/"))]
unique = {p.get_path_name():p for p in packages}
assert utils.save_packages(list(unique.values()),True)
remaining = list(utils.get_dirty_map_packages()) + list(utils.get_dirty_content_packages())
remaining_project = [p.get_path_name() for p in remaining if p.get_path_name().startswith(("/Game/", "/PrincipledNPR/"))]
assert not remaining_project, remaining_project
mesh = unreal.load_asset("/Game/NPR/Characters/Wanderer/Meshes/SKM_Wanderer")
assert mesh and mesh.get_editor_property("skeleton")
assert (project / "Content/NPR/Characters/Wanderer/Meshes/SKM_Wanderer_Skeleton.uasset").is_file()
record = {"status":"Saved", "saved_at":datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project":str(project / "PrincipledNPR.uproject"),"saved_dirty_packages":list(unique),
    "remaining_dirty_project_packages":remaining_project,
    "original_external_sources":"NotEdited",
    "migration_note":"Dirty migrated /Game/EndfieldRender packages, if listed, are now explicitly saved with UE 5.8. Initial copy hashes remain historical migration evidence, not current package hashes.",
    "upload_destination_policy":"Personal private GitHub backup; third-party assets must not be publicly redistributed"}
path = project / "Documentation/GitHubBackup.ProjectSave.json"
path.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding="utf-8")
unreal.log("PNPR_PROJECT_SAVED_FOR_GITHUB: " + str(len(unique)) + " dirty project packages saved")
