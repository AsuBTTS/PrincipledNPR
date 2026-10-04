"""Read-only UE asset audit; run in the existing PrincipledNPR editor."""
import datetime
import json
import os
from pathlib import Path
import traceback
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (project / "PrincipledNPR.uproject").is_file(), "Wrong project"
if not (project / "PrincipledNPR.uproject").is_file():
    raise RuntimeError("Wrong project")
root = "/Game/EndfieldRender"
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([root], True)
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
skeletal = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
mel = unreal.MaterialEditingLibrary
report = {"status": "Running", "engine": unreal.SystemLibrary.get_engine_version(),
          "time_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
          "package_root": root, "source_engine": "5.6", "assets": [], "meshes": [],
          "textures": [], "material_instances": [], "missing_dependencies": [],
          "warnings": [], "face_contract": "Uncalibrated", "hair_contract": "Uncalibrated",
          "source_assets_saved_or_modified": False}

def path(obj):
    return obj.get_path_name() if obj else None

def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None

def vec(v):
    return {"x": v.x, "y": v.y, "z": v.z}

data = registry.get_assets_by_path(root, True)
options = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,
    include_soft_package_references=True, include_searchable_names=False,
    include_hard_management_references=False, include_soft_management_references=False)
missing = set()
for entry in data:
    package = str(entry.package_name)
    cls = str(entry.asset_class_path.asset_name)
    report["assets"].append({"package": package, "class": cls})
    for dep in registry.get_dependencies(entry.package_name, options):
        dep = str(dep)
        if dep.startswith("/") and not dep.startswith(("/Script/", "/Memory/", "/Temp/")) and not assets.does_asset_exist(dep):
            missing.add(dep)
    try:
        if cls == "SkeletalMesh":
            mesh = assets.load_asset(package)
            component = unreal.new_object(unreal.SkeletalMeshComponent)
            component.set_skeletal_mesh_asset(mesh)
            bones = []
            for i in range(component.get_num_bones()):
                name = component.get_bone_name(i)
                bones.append({"index": i, "name": str(name), "parent": str(component.get_parent_bone(name))})
            slots = []
            for i, slot in enumerate(mesh.get_editor_property("materials")):
                slots.append({"index": i, "slot": str(slot.material_slot_name),
                              "imported_slot": str(prop(slot, "imported_material_slot_name")),
                              "material": path(slot.material_interface)})
            bounds = mesh.get_imported_bounds()
            lod_count = skeletal.get_lod_count(mesh)
            report["meshes"].append({"path": path(mesh), "skeleton": path(prop(mesh, "skeleton")),
                "bounds_origin_cm": vec(bounds.origin), "bounds_half_extent_cm": vec(bounds.box_extent),
                "lods": [{"index": i, "vertices": skeletal.get_num_verts(mesh, i),
                          "sections": skeletal.get_num_sections(mesh, i)} for i in range(lod_count)],
                "materials": slots, "bones": bones,
                "head_bone_candidates": [b["name"] for b in bones if "head" in b["name"].lower() or "头" in b["name"]]})
        elif cls == "Texture2D":
            texture = assets.load_asset(package)
            report["textures"].append({"path": path(texture),
                "width": texture.blueprint_get_size_x(), "height": texture.blueprint_get_size_y(),
                "srgb": prop(texture, "srgb"), "compression": str(prop(texture, "compression_settings")),
                "address_x": str(prop(texture, "address_x")), "address_y": str(prop(texture, "address_y")),
                "lod_group": str(prop(texture, "lod_group")),
                "semantic_from_filename": "Unverified"})
        elif cls == "MaterialInstanceConstant":
            material = assets.load_asset(package)
            texture_params = [{"name": str(n), "texture": path(mel.get_material_instance_texture_parameter_value(material, n))}
                              for n in mel.get_texture_parameter_names(material)]
            scalar_params = [{"name": str(n), "value": mel.get_material_instance_scalar_parameter_value(material, n)}
                             for n in mel.get_scalar_parameter_names(material)]
            report["material_instances"].append({"path": path(material), "parent": path(prop(material, "parent")),
                "effective_texture_parameters": texture_params, "effective_scalar_parameters": scalar_params})
    except Exception:
        report["warnings"].append({"package": package, "error": traceback.format_exc()})

report["missing_dependencies"] = sorted(missing)
report["status"] = "Audited" if not report["warnings"] and not missing else "NeedsReview"
report["asset_count"] = len(report["assets"])
output = project / "Documentation" / "Assets" / "ChenQianyu.UE58.audit.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("PNPR_ASSET_AUDIT: " + json.dumps({"status": report["status"], "assets": len(data),
    "meshes": len(report["meshes"]), "textures": len(report["textures"]),
    "material_instances": len(report["material_instances"]), "warnings": len(report["warnings"]),
    "missing_dependencies": sorted(missing)}, ensure_ascii=False))
