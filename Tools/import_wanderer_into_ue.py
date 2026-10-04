"""Import the converted PMX fixture as a real UE skeletal mesh and textures."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import traceback
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (PROJECT / "PrincipledNPR.uproject").is_file(), "Wrong project"
ROOT = "/Game/NPR/Characters/Wanderer"
MESH_PATH = ROOT + "/Meshes/SKM_Wanderer"
OWNER = "PrincipledNPR.WandererImport.v1"
TAG = "PrincipledNPRWandererImportOwner"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {"owner": OWNER, "status": "Running", "mesh": MESH_PATH,
    "imported_assets": [], "textures": [], "formal_animation_validation": "NotRun"}
output = PROJECT / "Documentation/Assets/Wanderer.UE58.import.json"

def require(value, message):
    if not value:
        raise RuntimeError(message)

def flush():
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

def load_owned(path, cls):
    if not assets.does_asset_exist(path):
        return None
    obj = assets.load_asset(path)
    require(isinstance(obj, cls) and assets.get_metadata_tag(obj, TAG) == OWNER, "Unowned/wrong asset: " + path)
    return obj

def imported(task):
    paths = [str(p) for p in task.get_editor_property("imported_object_paths")]
    require(paths, "Import returned no asset paths: " + task.get_editor_property("filename"))
    for path in paths:
        obj = assets.load_asset(path)
        require(obj, "Cannot load imported asset")
        assets.set_metadata_tag(obj, TAG, OWNER)
        require(assets.save_loaded_asset(obj, False), "Save failed")
        report["imported_assets"].append({"path": obj.get_path_name(), "class": obj.get_class().get_name()})

def task_for(source, destination, name):
    task = unreal.AssetImportTask()
    for name_, value in (("filename", str(source)), ("destination_path", destination), ("destination_name", name),
                         ("automated", True), ("replace_existing", False), ("save", True), ("async_", False)):
        task.set_editor_property(name_, value)
    return task

def main():
    actual = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    require(os.path.normcase(str(actual)) == os.path.normcase(str(PROJECT)), "Wrong project")
    report["engine"] = unreal.SystemLibrary.get_engine_version()
    require(report["engine"].startswith("5.8.3-58210709"), "Wrong engine")
    conversion = json.loads((PROJECT / "Documentation/Assets/Wanderer.Conversion.json").read_text(encoding="utf-8"))
    require(conversion["status"] == "Converted_UEImportPending", "Conversion failed/incomplete")
    fbx = Path(conversion["fbx"]["path"])
    require(hashlib.sha256(fbx.read_bytes()).hexdigest() == conversion["fbx"]["sha256"], "FBX changed after conversion")
    mesh = load_owned(MESH_PATH, unreal.SkeletalMesh)
    if mesh is None:
        options = unreal.FbxImportUI()
        for key, value in (("import_as_skeletal", True), ("import_mesh", True), ("import_materials", False),
                           ("import_textures", False), ("import_animations", False), ("create_physics_asset", False),
                           ("automated_import_should_detect_type", False), ("override_full_name", True),
                           ("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)):
            options.set_editor_property(key, value)
        mesh_options = options.get_editor_property("skeletal_mesh_import_data")
        for key, value in (("import_morph_targets", True), ("convert_scene", True), ("convert_scene_unit", True),
                           ("force_front_x_axis", True), ("use_t0_as_ref_pose", False),
                           ("import_meshes_in_bone_hierarchy", True), ("preserve_smoothing_groups", True),
                           ("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)):
            mesh_options.set_editor_property(key, value)
        task = task_for(fbx, ROOT + "/Meshes", "SKM_Wanderer")
        task.set_editor_property("factory", unreal.FbxFactory())
        task.set_editor_property("options", options)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        cvar = "Interchange.FeatureFlags.Import.FBX"
        previous = unreal.SystemLibrary.get_console_variable_int_value(cvar)
        report["import_pipeline"] = "Explicit legacy FbxFactory; scoped Interchange FBX switch restored afterward"
        try:
            unreal.SystemLibrary.execute_console_command(world, cvar + " 0")
            require(unreal.SystemLibrary.get_console_variable_int_value(cvar) == 0, "Cannot select legacy import")
            tools.import_asset_tasks([task])
            imported(task)
        finally:
            unreal.SystemLibrary.execute_console_command(world, cvar + " " + str(previous))
            report["interchange_fbx_restored"] = unreal.SystemLibrary.get_console_variable_int_value(cvar) == previous
        mesh = load_owned(MESH_PATH, unreal.SkeletalMesh)
    require(mesh, "Expected skeletal mesh missing")
    skeleton = mesh.get_editor_property("skeleton")
    require(skeleton and skeleton.get_path_name() == MESH_PATH + "_Skeleton.SKM_Wanderer_Skeleton",
            "Unexpected generated skeleton; do not save an unrelated asset")
    require(assets.get_metadata_tag(skeleton, "PrincipledNPRWandererImportOwner") in ("", OWNER),
            "Unowned skeleton")
    assets.set_metadata_tag(skeleton, "PrincipledNPRWandererImportOwner", OWNER)
    require(assets.save_loaded_asset(skeleton, False), "Skeleton save failed")
    report["skeleton_persistence"] = {"asset": skeleton.get_path_name(), "saved_to_canonical_package": True}
    texture_names = {"tex/颜.png": "T_Wanderer_Face", "tex/颜2.png": "T_Wanderer_FaceAlt",
        "tex/髮.png": "T_Wanderer_Hair", "tex/体.png": "T_Wanderer_Body",
        "tex/肌.png": "T_Wanderer_Skin", "tex/spa_h.png": "T_Wanderer_HairOverlay", "tex/AL.png": "T_Wanderer_Effect"}
    for relative, name in texture_names.items():
        package = ROOT + "/Textures/" + name
        texture = load_owned(package, unreal.Texture2D)
        if texture is None:
            source = PROJECT / "SourceAssets/Wanderer/Original" / relative
            task = task_for(source, ROOT + "/Textures", name)
            tools.import_asset_tasks([task])
            imported(task)
            texture = load_owned(package, unreal.Texture2D)
        require(texture, "Texture import missing")
        texture.set_editor_property("srgb", True)
        require(assets.save_loaded_asset(texture, False), "Texture save failed")
        report["textures"].append({"source_relative": relative, "asset": texture.get_path_name(),
            "width": texture.blueprint_get_size_x(), "height": texture.blueprint_get_size_y(), "srgb": True})
    slots = [{"index": i, "slot_name": str(s.material_slot_name),
              "imported_slot_name": str(s.get_editor_property("imported_material_slot_name"))}
             for i, s in enumerate(mesh.get_editor_property("materials"))]
    require(len(slots) == 26, "Material sections changed; require explicit remapping")
    component = unreal.new_object(unreal.SkeletalMeshComponent)
    component.set_skeletal_mesh_asset(mesh)
    bones = [{"index": i, "name": str(component.get_bone_name(i)),
              "parent": str(component.get_parent_bone(component.get_bone_name(i)))}
             for i in range(component.get_num_bones())]
    require(any(b["name"] == "B_132" for b in bones), "Head bone missing")
    require(sum(b["parent"] == "None" for b in bones) == 1, "Expected one UE skeleton root")
    bounds = mesh.get_imported_bounds()
    height = bounds.box_extent.z * 2
    require(160 < height < 175, "Unexpected UE size/orientation: height=%s cm" % height)
    skeletal = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    report["audit"] = {"skeleton": mesh.get_editor_property("skeleton").get_path_name(),
        "lod0_vertices": skeletal.get_num_verts(mesh, 0), "lod0_sections": skeletal.get_num_sections(mesh, 0),
        "bone_count": len(bones), "bones": bones, "head_bone": "B_132", "head_source_name": "頭",
        "height_cm": height, "bounds_origin_cm": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
        "bounds_half_extent_cm": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
        "material_slots": slots, "morph_targets": [m.get_name() for m in mesh.get_editor_property("morph_targets")]}
    report["status"] = "ImportedAndMetadataChecked_VisualPending"
    report["completed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    flush()
    unreal.log("PNPR_WANDERER_IMPORTED: " + json.dumps({"mesh": MESH_PATH, "height_cm": height,
        "bones": len(bones), "slots": len(slots), "morphs": len(report["audit"]["morph_targets"])}, ensure_ascii=False))

try:
    main()
except Exception:
    report["status"] = "Failed"
    report["error"] = traceback.format_exc()
    flush()
    raise
