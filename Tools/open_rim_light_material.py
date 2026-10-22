"""Refresh only the Rim asset folder and open its preview in the current editor."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (root / "PrincipledNPR.uproject").is_file(), "Wrong project"
folder = "/PrincipledNPR/RimLight"
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([folder], True)
path = folder + "/MI_RimLight_Preview"
asset = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).load_asset(path)
assert asset, "Create the Rim Light assets first"
unreal.EditorAssetLibrary.sync_browser_to_objects([path])
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([asset])
unreal.MaterialEditingLibrary.refresh_material_instance_editor(asset)
unreal.log("Rim Light preview opened: four material controls; no level or actor binding changed.")
