"""Native S58 character smoke scene. Face/Hair specialized models are not implemented.

Uses component material overrides; never edits the migrated meshes/materials.
Re-running reuses owned assets and preserves existing completed actor edits.
"""
import datetime
import json
import os
from pathlib import Path
import traceback
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (PROJECT / "PrincipledNPR.uproject").is_file(), "Wrong project"
ROOT = "/Game/NPR/Characters/ChenQianyu"
MAP = "/Game/NPR/Maps/L_ChenQianyu_AssetProbe"
OWNER = "PrincipledNPR.CharacterAssetProbe.v1"
TAG = "PrincipledNPRCharacterProbeOwner"
READY = "PrincipledNPRCharacterProbeReady"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
record = {"status": "Running", "map": MAP, "backend": "S58_NativeGeneric_Diagnostic",
    "face_model": "NotImplemented", "hair_model": "NotImplemented", "style_adapter": "NotImplemented",
    "formal_lighting_tests": "NotRun", "assets": [], "actors": [], "slots": [], "compile_requests": []}
output = PROJECT / "Documentation" / "Assets" / "ChenQianyu.ProbeCreation.json"

def require(value, message):
    if not value:
        raise RuntimeError(message)

def flush():
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")

def own(obj):
    assets.set_metadata_tag(obj, TAG, OWNER)

def check(obj):
    require(assets.get_metadata_tag(obj, TAG) == OWNER, "Refusing to modify unowned asset: " + obj.get_path_name())

def save(obj):
    require(assets.save_loaded_asset(obj, False), "Asset save failed: " + obj.get_path_name())

def create(name, cls, factory):
    package = ROOT + "/Materials/" + name
    if assets.does_asset_exist(package):
        obj = assets.load_asset(package)
        check(obj)
        require(isinstance(obj, cls), "Wrong asset class")
        new = False
    else:
        obj = tools.create_asset(name, ROOT + "/Materials", cls, factory())
        require(obj, "Factory failed")
        own(obj)
        new = True
    record["assets"].append({"path": obj.get_path_name(), "created": new})
    return obj, new or assets.get_metadata_tag(obj, READY) != "1"

def node(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)

def scalar(mat, name, value, x, y):
    n = node(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", value)
    return n

def connect(source, dest, semantic):
    names = [str(n) for n in mel.get_material_expression_input_names(dest)]
    match = [n for n in names if n.replace(" ", "").lower() == semantic.replace(" ", "").lower()]
    require(len(match) == 1 and mel.connect_material_expressions(source, "", dest, match[0]), "Missing pin: " + semantic)

def parent(suffix):
    mat, initialize = create("M_S58_CharacterGeneric_" + suffix, unreal.Material, unreal.MaterialFactoryNew)
    if initialize:
        # Only rebuild incomplete assets created by this script.
        mel.delete_all_material_expressions(mat)
        sample = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -500, -150)
        sample.set_editor_property("parameter_name", "BaseColorTexture")
        sample.set_editor_property("texture", assets.load_asset("/Game/EndfieldRender/Character/Qianyu/Tex/T_actor_chen_cloth_01_D"))
        front = node(mat, unreal.MaterialExpressionSubstrateToonBSDF, -100, 0)
        front.set_editor_property("toon_profile", assets.load_asset("/Game/NPR/Profiles/TP_S58_NativeBaseline_" + suffix))
        connect(sample, front, "BaseColor")
        connect(scalar(mat, "Roughness", 0.65, -500, 130), front, "Roughness")
        connect(scalar(mat, "Specular", 0.35, -500, 250), front, "Specular")
        require(mel.connect_material_property(front, "", unreal.MaterialProperty.MP_FRONT_MATERIAL), "Front Material failed")
        errors = mel.recompile_material(mat)
        record["compile_requests"].append({"material": mat.get_path_name(), "immediate_errors": [str(e) for e in (errors or [])]})
        require(not errors, "Compile errors")
        assets.set_metadata_tag(mat, READY, "1")
        save(mat)
    return mat

def invisible_material():
    mat, initialize = create("M_ExcludedSourceProxy", unreal.Material, unreal.MaterialFactoryNew)
    if initialize:
        mel.delete_all_material_expressions(mat)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        front = node(mat, unreal.MaterialExpressionSubstrateToonBSDF, -100, 0)
        front.set_editor_property("toon_profile", assets.load_asset("/Game/NPR/Profiles/TP_S58_NativeBaseline_A"))
        zero = node(mat, unreal.MaterialExpressionConstant, -400, 200)
        zero.set_editor_property("r", 0.0)
        require(mel.connect_material_property(zero, "", unreal.MaterialProperty.MP_OPACITY_MASK), "Mask failed")
        require(mel.connect_material_property(front, "", unreal.MaterialProperty.MP_FRONT_MATERIAL), "Front failed")
        errors = mel.recompile_material(mat)
        record["compile_requests"].append({"material": mat.get_path_name(), "immediate_errors": [str(e) for e in (errors or [])]})
        require(not errors, "Compile errors")
        assets.set_metadata_tag(mat, READY, "1")
        save(mat)
    return mat

def instance(index, suffix, parent_mat, texture):
    obj, initialize = create("MI_Chen_Slot%02d_%s" % (index, suffix), unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew)
    if initialize:
        mel.set_material_instance_parent(obj, parent_mat)
        # UE 5.8.3 MaterialEditingLibrary.cpp returns false unconditionally in
        # this setter. Validate the actual readback rather than that return.
        mel.set_material_instance_texture_parameter_value(obj, "BaseColorTexture", texture)
        readback = mel.get_material_instance_texture_parameter_value(obj, "BaseColorTexture")
        require(readback and readback.get_path_name() == texture.get_path_name(), "MI texture readback failed")
        assets.set_metadata_tag(obj, READY, "1")
        save(obj)
    return obj

def actor(key, cls, xyz, rotation=None):
    tag = "PNPR_CharacterProbe_" + key
    found = [a for a in actors.get_all_level_actors() if tag in [str(t) for t in a.get_editor_property("tags")]]
    require(len(found) <= 1, "Duplicate actor tag")
    if found:
        require(isinstance(found[0], cls), "Wrong actor class")
        record["actors"].append({"key": key, "path": found[0].get_path_name(), "created": False})
        return found[0], "PNPR_CharacterProbe_Ready" not in [str(t) for t in found[0].get_editor_property("tags")]
    obj = actors.spawn_actor_from_class(cls, unreal.Vector(*xyz), rotation or unreal.Rotator(), False)
    obj.set_actor_label("PNPR_" + key)
    obj.set_editor_property("tags", [unreal.Name(tag)])
    record["actors"].append({"key": key, "path": obj.get_path_name()})
    return obj, True

def complete_actor(obj):
    tags = list(obj.get_editor_property("tags"))
    if "PNPR_CharacterProbe_Ready" not in [str(t) for t in tags]:
        obj.set_editor_property("tags", tags + [unreal.Name("PNPR_CharacterProbe_Ready")])

def main():
    actual = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    require(os.path.normcase(str(actual)) == os.path.normcase(str(PROJECT)), "Wrong project")
    require(unreal.SystemLibrary.get_engine_version().startswith("5.8.3-58210709"), "Wrong engine")
    audit = json.loads((PROJECT / "Documentation/Assets/ChenQianyu.UE58.audit.json").read_text(encoding="utf-8"))
    require(audit["status"] == "Audited", "Asset audit incomplete")
    mesh = assets.load_asset("/Game/EndfieldRender/Character/Qianyu/SKM_Chen")
    require(mesh and len(mesh.get_editor_property("materials")) == 11, "Unexpected mesh slot layout")
    parents = {suffix: parent(suffix) for suffix in ("A", "B")}
    hidden = invisible_material()
    excluded = {3: "Embedded source outline", 5: "Eye shadow proxy", 10: "Hair shadow proxy"}
    overrides = {"A": [], "B": []}
    for index, slot in enumerate(mesh.get_editor_property("materials")):
        if index in excluded:
            record["slots"].append({"index": index, "name": str(slot.material_slot_name), "probe_policy": "Excluded", "reason": excluded[index]})
            for suffix in overrides:
                overrides[suffix].append(hidden)
            continue
        source_material = slot.material_interface
        texture = mel.get_material_instance_texture_parameter_value(source_material, "T_Base_Color") if isinstance(source_material, unreal.MaterialInstanceConstant) else None
        fallback = not bool(texture)
        if fallback:
            require(index == 4, "No verified BaseColor texture for slot %d" % index)
            texture = assets.load_asset("/Game/EndfieldRender/Character/Qianyu/Tex/T_actor_chen_cloth_01_D")
        record["slots"].append({"index": index, "name": str(slot.material_slot_name),
            "source_material": source_material.get_path_name(), "base_color_texture": texture.get_path_name(),
            "texture_evidence": "Explicit cloth fallback; needs visual verification" if fallback else "Effective T_Base_Color parameter",
            "probe_policy": "GenericToonOnly", "specialized_model": "NotImplemented"})
        for suffix in overrides:
            overrides[suffix].append(instance(index, suffix, parents[suffix], texture))
    if assets.does_asset_exist(MAP):
        world = assets.load_asset(MAP)
        check(world)
        require(levels.load_level(MAP), "Cannot load owned map")
    else:
        require(levels.new_level(MAP, False), "Cannot create map")
        own(editor.get_editor_world())
        require(levels.save_current_level(), "Cannot save map")
    for suffix, x in (("A", -85.0), ("B", 85.0)):
        obj, new = actor("ChenQianyu_S58_" + suffix, unreal.SkeletalMeshActor, (x, 0, 0))
        if new:
            comp = obj.get_component_by_class(unreal.SkeletalMeshComponent)
            comp.set_skeletal_mesh_asset(mesh)
            comp.set_cast_shadow(True)
            for index, material in enumerate(overrides[suffix]):
                comp.set_material(index, material)
        complete_actor(obj)
    floor, new = actor("CharacterProbe_Floor", unreal.StaticMeshActor, (0, 0, -4))
    if new:
        comp = floor.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_static_mesh(assets.load_asset("/Engine/BasicShapes/Cube"))
        comp.set_material(0, assets.load_asset("/Game/NPR/Materials/M_S58_NativeSlab_Floor"))
        floor.set_actor_scale3d(unreal.Vector(6, 5, 0.08))
    complete_actor(floor)
    light, new = actor("CharacterProbe_Key", unreal.DirectionalLight, (0, 0, 300), unreal.Rotator(pitch=-35, yaw=-100, roll=0))
    if new:
        comp = light.get_component_by_class(unreal.DirectionalLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_intensity(3.14159265)
        comp.set_cast_shadows(True)
        comp.set_light_source_angle(0.0)
    complete_actor(light)
    sky, new = actor("CharacterProbe_Sky", unreal.SkyLight, (0, 0, 300))
    if new:
        comp = sky.get_component_by_class(unreal.SkyLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        comp.set_cubemap(assets.load_asset("/Engine/EngineResources/GrayLightTextureCube"))
        comp.set_intensity(0.25)
    complete_actor(sky)
    pp, new = actor("CharacterProbe_FixedExposure", unreal.PostProcessVolume, (0, 0, 0))
    if new:
        pp.set_editor_property("unbound", True)
        settings = pp.get_editor_property("settings")
        for name, value in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False),
                ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.0)):
            settings.set_editor_property(name, value)
        pp.set_editor_property("settings", settings)
    complete_actor(pp)
    camera_location = unreal.Vector(0, 280, 140)
    camera_rotation = unreal.Rotator(pitch=-10, yaw=-90, roll=0)
    camera, _ = actor("CharacterProbe_Camera", unreal.CameraActor, (0, 280, 140), camera_rotation)
    complete_actor(camera)
    editor.set_level_viewport_camera_info(camera_location, camera_rotation)
    require(levels.save_current_level(), "Final map save failed")
    record["status"] = "Created_NotVisuallyQualified"
    record["completed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    flush()
    unreal.log("PNPR_CHARACTER_PROBE_CREATED: " + MAP)

try:
    main()
except Exception:
    record["status"] = "Failed"
    record["error"] = traceback.format_exc()
    flush()
    raise
