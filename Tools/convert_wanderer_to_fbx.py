"""Run with installed Blender --background --factory-startup --python.
Creates project-local PMX provenance, editable blend and FBX. No user prefs saved.
"""
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import sys
import traceback
import addon_utils
import bpy

PROJECT = Path(__file__).resolve().parent.parent
SOURCE = PROJECT / "SourceAssets/Wanderer/Original"
OUT = PROJECT / "SourceAssets/Wanderer"
DOCS = PROJECT / "Documentation/Assets"
REPORT = DOCS / "Wanderer.Conversion.json"
OWNER = "PrincipledNPR.WandererConversion.v1"
report = {"owner": OWNER, "status": "Running", "blender": bpy.app.version_string,
    "blender_build_hash": bpy.app.build_hash.decode(), "source": str(SOURCE),
    "pmx_import_scale_meters_per_unit": 0.08, "fbx_unit_policy": "Meters in Blender; FBX unit metadata; UE converts to centimeters",
    "source_files": [], "bones": [], "materials": [], "limitations": [
        "No PMX rigid body/joint simulation or VMD animation imported.",
        "UE linear skinning does not reproduce PMX SDEF/QDEF exactly.",
        "Only vertex shape keys are exported; bone/material/UV/group morph logic is not implemented in UE.",
        "MMD toon and sphere-map lighting are replaced by native UE diagnostic shading."]}

def flush():
    DOCS.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def main():
    marker = OUT / "owner.json"
    if OUT.exists():
        require(marker.exists() and json.loads(marker.read_text(encoding="utf-8"))["owner"] == OWNER,
                "Refuse to overwrite unowned source-conversion directory")
    OUT.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps({"owner": OWNER}), encoding="utf-8")
    original = OUT / "Original"
    for file in sorted(SOURCE.rglob("*")):
        if not file.is_file():
            continue
        rel = file.relative_to(SOURCE)
        target = original / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        expected = hashlib.sha256(file.read_bytes()).hexdigest()
        if target.exists():
            require(hashlib.sha256(target.read_bytes()).hexdigest() == expected, "Copied source changed: " + str(rel))
        else:
            shutil.copy2(file, target)
        require(hashlib.sha256(target.read_bytes()).hexdigest() == expected, "Copy mismatch")
        report["source_files"].append({"relative": str(rel), "bytes": file.stat().st_size, "sha256": expected})
    flush()
    module = "bl_ext.blender_org.mmd_tools"
    mmd = addon_utils.enable(module, default_set=False, persistent=False)
    if mmd is None:
        module = "mmd_tools"
        extension_root = Path(bpy.utils.user_resource("CONFIG")).parent / "extensions/blender_org"
        sys.path.insert(0, str(extension_root))
        mmd = addon_utils.enable(module, default_set=False, persistent=False)
    require(mmd is not None, "Installed MMD Tools could not be loaded")
    report["mmd_tools_version"] = mmd.MMD_TOOLS_VERSION
    addon_utils.enable("io_scene_fbx", default_set=False, persistent=False)
    # Clear only this factory-startup background scene.
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    result = bpy.ops.mmd_tools.import_model(filepath=str(original / "流浪者.pmx"),
        types={"MESH", "ARMATURE", "MORPHS"}, scale=0.08, clean_model=False,
        remove_doubles=False, rename_bones=False, fix_ik_links=False, save_log=False,
        log_level="WARNING")
    require("FINISHED" in result, "PMX import operator failed")
    armatures = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and o.find_armature() is not None]
    require(len(armatures) == 1 and len(meshes) == 1, "Expected one armature and one skinned mesh")
    rig, mesh = armatures[0], meshes[0]
    require(len(mesh.data.vertices) == 27650 and len(mesh.data.polygons) == 32747,
            "PMX topology differs from audited input")
    require(len(mesh.data.materials) == 26, "PMX material table differs")
    # ASCII bone IDs have an explicit lossless source-name mapping. Constraints
    # and vertex groups are updated by Blender when a bone is renamed.
    for i, bone in enumerate(list(rig.data.bones)):
        pose = rig.pose.bones[bone.name]
        info = {"index": i, "source_name": bone.name,
                "pmx_name_j": pose.mmd_bone.name_j, "pmx_name_e": pose.mmd_bone.name_e,
                "pmx_bone_id": pose.mmd_bone.bone_id,
                "source_parent": bone.parent.name if bone.parent else None,
                "head_blender_m": list(bone.head_local), "tail_blender_m": list(bone.tail_local)}
        bone.name = "B_%03d" % i
        info["export_name"] = bone.name
        report["bones"].append(info)
    # UE skeletal meshes require a single root. MMD Tools may create several
    # root/helper chains; parent them to one neutral root without moving them.
    bpy.ops.object.select_all(action="DESELECT")
    rig.hide_set(False)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    roots = [b for b in rig.data.edit_bones if b.parent is None]
    report["root_chains_before_normalization"] = [b.name for b in roots]
    root = rig.data.edit_bones.new("PNPR_Root")
    root.head = (0, 0, 0)
    root.tail = (0, 0, 0.1)
    for bone in roots:
        bone.parent = root
        bone.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    for info in report["bones"]:
        bone = rig.data.bones[info["export_name"]]
        info["export_parent"] = bone.parent.name if bone.parent else None
    report["bones"].append({"index": len(report["bones"]), "source_name": None,
        "pmx_name_j": None, "pmx_name_e": None, "pmx_bone_id": None,
        "export_name": "PNPR_Root", "export_parent": None, "role": "Authored neutral single root"})
    report["root_normalization"] = "Added PNPR_Root; preserved rest transforms"
    rig.name = "Armature"
    rig.data.name = "WandererSkeleton"
    rig.data.pose_position = "REST"
    mesh.name = "WandererMesh"
    mesh.data.name = "WandererGeometry"
    original_audit = json.loads((DOCS / "Wanderer.PMX.audit.json").read_text(encoding="utf-8"))
    for i, mat in enumerate(mesh.data.materials):
        info = dict(original_audit["materials"][i])
        info["index"] = i
        info["blender_source_material"] = mat.name
        mat.name = "M_Wanderer_%02d" % i
        info["fbx_material_name"] = mat.name
        report["materials"].append(info)
    removed_keys = []
    if mesh.data.shape_keys:
        for key in list(mesh.data.shape_keys.key_blocks):
            if key.name.startswith("mmd_sdef_"):
                removed_keys.append(key.name)
                mesh.shape_key_remove(key)
    report["excluded_internal_sdef_shape_keys"] = removed_keys
    keys = list(mesh.data.shape_keys.key_blocks) if mesh.data.shape_keys else []
    report["shape_keys"] = [{"index": i, "name": k.name} for i, k in enumerate(keys)]
    report["vertex_shape_key_count_excluding_basis"] = max(0, len(keys) - 1)
    report["vertex_count"] = len(mesh.data.vertices)
    report["triangle_count"] = len(mesh.data.polygons)
    report["uv_layers"] = [layer.name for layer in mesh.data.uv_layers]
    report["bone_count_in_blender"] = len(rig.data.bones)
    report["zero_weight_vertex_count"] = sum(not any(g.weight > 0 for g in v.groups) for v in mesh.data.vertices)
    report["max_vertex_influences"] = max(sum(g.weight > 0 for g in v.groups) for v in mesh.data.vertices)
    coords = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
    report["bounds_blender_m"] = {"min": [min(v[i] for v in coords) for i in range(3)],
                                 "max": [max(v[i] for v in coords) for i in range(3)]}
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 1.0
    converted = OUT / "Converted"
    converted.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(converted / "Wanderer.blend"), check_existing=False)
    bpy.ops.file.make_paths_relative()
    bpy.ops.wm.save_as_mainfile(filepath=str(converted / "Wanderer.blend"), check_existing=False)
    bpy.ops.object.select_all(action="DESELECT")
    rig.hide_set(False)
    mesh.hide_set(False)
    rig.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = rig
    fbx = converted / "SKM_Wanderer.fbx"
    settings = {"filepath": str(fbx), "use_selection": True, "object_types": {"ARMATURE", "MESH"},
        "global_scale": 1.0, "apply_unit_scale": True, "apply_scale_options": "FBX_SCALE_UNITS",
        "axis_forward": "-Y", "axis_up": "Z", "use_space_transform": True,
        "bake_space_transform": False, "use_mesh_modifiers": False, "mesh_smooth_type": "FACE",
        "use_tspace": True, "add_leaf_bones": False, "use_armature_deform_only": False,
        "primary_bone_axis": "Y", "secondary_bone_axis": "X", "armature_nodetype": "NULL",
        "bake_anim": False, "path_mode": "RELATIVE", "embed_textures": False, "use_custom_props": False}
    require("FINISHED" in bpy.ops.export_scene.fbx(**settings), "FBX export failed")
    report["fbx_export_settings"] = {k: sorted(v) if isinstance(v, set) else v for k, v in settings.items()}
    report["fbx"] = {"path": str(fbx), "bytes": fbx.stat().st_size,
                     "sha256": hashlib.sha256(fbx.read_bytes()).hexdigest()}
    report["status"] = "Converted_UEImportPending"
    report["completed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    flush()
    print("PNPR_WANDERER_CONVERTED " + json.dumps({k: report[k] for k in
        ("vertex_count", "triangle_count", "bone_count_in_blender", "vertex_shape_key_count_excluding_basis", "zero_weight_vertex_count")}, ensure_ascii=False))

try:
    main()
except Exception:
    report["status"] = "Failed"
    report["error"] = traceback.format_exc()
    flush()
    raise
