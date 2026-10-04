"""Two real character fixtures with explicitly uncalibrated diffuse style seeds.
Keeps semantic JSON StyleProfile separate from native curves/ToonProfile assets.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import traceback
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (PROJECT / "PrincipledNPR.uproject").is_file(), "Wrong project"
MAP = "/Game/NPR/Maps/L_TwoStyle_CharacterProbe"
OWNER = "PrincipledNPR.TwoStyleCharacterProbe.v1"
TAG = "PrincipledNPRTwoStyleOwner"
READY = "PrincipledNPRTwoStyleReady"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
mel = unreal.MaterialEditingLibrary
report = {"owner": OWNER, "status": "Running", "map": MAP, "assets": [], "actors": [], "compile_requests": [],
    "character_slots": {}, "style_bindings": [], "face_model": "NotImplemented", "hair_model": "NotImplemented",
    "formal_lighting_tests": "NotRun", "adapter_scope": "Initial diffuse-only seed; not a full semantic backend adapter"}
output = PROJECT / "Documentation/Assets/TwoStyleCharacterProbe.Creation.json"

def require(value, message):
    if not value:
        raise RuntimeError(message)

def flush():
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

def save(obj):
    require(assets.save_loaded_asset(obj, False), "Save failed: " + obj.get_path_name())

def create(folder, name, cls, factory):
    package = folder + "/" + name
    if assets.does_asset_exist(package):
        obj = assets.load_asset(package)
        require(isinstance(obj, cls) and assets.get_metadata_tag(obj, TAG) == OWNER, "Unowned/wrong asset: " + package)
        initialize = assets.get_metadata_tag(obj, READY) != "1"
    else:
        obj = tools.create_asset(name, folder, cls, factory())
        require(obj, "Cannot create " + package)
        assets.set_metadata_tag(obj, TAG, OWNER)
        initialize = True
    report["assets"].append({"path": obj.get_path_name(), "class": obj.get_class().get_name()})
    return obj, initialize

def complete(obj):
    assets.set_metadata_tag(obj, READY, "1")
    save(obj)

def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)

def diffuse_curve(style_id, source):
    curve, initialize = create("/Game/NPR/Styles/" + style_id + "/Curves", "C_Diffuse_Initial",
                               unreal.CurveLinearColor, unreal.CurveLinearColorFactory)
    center = source["defaultsPatch"]["TerminatorDotCenter"]["value"]
    width = source["defaultsPatch"]["TerminatorDotHalfWidth"]["value"]
    shadow_level = 0.28 if style_id == "AnimeFlat" else 0.18
    if initialize:
        # Python excludes these fixed FRichCurve arrays. The stock CSV factory
        # calls UCurveBase::CreateCurveFromCSVString and writes linear keys.
        rows = []
        for i in range(257):
            u = i / 256.0
            q = 2.0 * u - 1.0
            response = shadow_level + (1.0 - shadow_level) * smooth((q - center + width) / (2.0 * width))
            rows.append("%.9f,%.9f,%.9f,%.9f,1" % (u, response, response, response))
        csv_path = PROJECT / "SourceAssets/BackendSeeds" / style_id / "C_Diffuse_Initial.csv"
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        csv_path.write_text("\n".join(rows) + "\n", encoding="utf-8")
        factory = unreal.CSVImportFactory()
        settings = factory.get_editor_property("automated_import_settings")
        enums = [getattr(unreal.CSVImportType, n) for n in dir(unreal.CSVImportType)
                 if "LINEAR" in n and "COLOR" in n]
        require(len(enums) == 1, "Cannot resolve CSV CurveLinearColor import enum")
        settings.set_editor_property("import_type", enums[0])
        settings.set_editor_property("import_curve_interp_mode", unreal.RichCurveInterpMode.RCIM_LINEAR)
        factory.set_editor_property("automated_import_settings", settings)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(csv_path))
        task.set_editor_property("destination_path", "/Game/NPR/Styles/" + style_id + "/Curves")
        task.set_editor_property("destination_name", "C_Diffuse_Initial")
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        task.set_editor_property("factory", factory)
        tools.import_asset_tasks([task])
        curve = assets.load_asset("/Game/NPR/Styles/" + style_id + "/Curves/C_Diffuse_Initial")
        require(isinstance(curve, unreal.CurveLinearColor), "CSV curve import failed")
        assets.set_metadata_tag(curve, TAG, OWNER)
        complete(curve)
    samples = [curve.get_unadjusted_linear_color_value(u).r for u in (0.0, 0.25, 0.5, 0.75, 1.0)]
    require(abs(samples[0] - shadow_level) < 0.001 and abs(samples[-1] - 1.0) < 0.001,
            "Curve readback endpoints incorrect")
    require(all(a <= b for a, b in zip(samples, samples[1:])), "Curve not monotonic")
    return curve, {"signed_dot_center": center, "signed_dot_half_width": width,
                   "shadow_multiplier": shadow_level, "readback_samples": samples,
                   "mapping_grade": "Approximate after native LUT sampling; artistic shadow multiplier authored here"}

def style_profile(style_id, reference_label):
    source_path = PROJECT / ("Documentation/Styles/" + style_id + ".v1.draft.json")
    source = json.loads(source_path.read_text(encoding="utf-8"))
    curve, mapping = diffuse_curve(style_id, source)
    profile, initialize = create("/Game/NPR/Styles/" + style_id + "/Profiles", "TP_S58_InitialDiffuseSeed",
                                 unreal.ToonProfile, unreal.ToonProfileFactory)
    if initialize:
        settings = profile.get_editor_property("settings")
        ramp = settings.get_editor_property("diffuse_ramp")
        ramp.set_editor_property("external_curve", curve)
        settings.set_editor_property("diffuse_ramp", ramp)
        settings.set_editor_property("diffuse_ramp_include_shadow", False)
        settings.set_editor_property("diffuse_ramp_offset_strength", 0.0)
        settings.set_editor_property("specular_ramp_offset_strength", 0.0)
        settings.set_editor_property("diffuse_indirect_scale", source["renderingPolicy"]["indirectDiffuseScaleDesignTarget"])
        settings.set_editor_property("specular_indirect_scale", source["renderingPolicy"]["indirectSpecularScaleDesignTarget"])
        profile.set_editor_property("settings", settings)
        assets.set_metadata_tag(profile, "NPRSemanticStyleId", style_id)
        assets.set_metadata_tag(profile, "NPRReferenceDirection", reference_label)
        assets.set_metadata_tag(profile, "NPRCalibrationState", "Uncalibrated_DiffuseSeedOnly")
        complete(profile)
    report["style_bindings"].append({"semantic_style_id": style_id, "reference_direction": reference_label,
        "semantic_style_file": str(source_path), "semantic_source_sha256": hashlib.sha256(source_path.read_text(encoding='utf-8').encode('utf-8')).hexdigest(),
        "semantic_source_hash_normalization": "UTF8_LF",
        "toon_profile": profile.get_path_name(), "diffuse_curve": curve.get_path_name(), "diffuse_mapping": mapping,
        "unmapped_contracts": ["AngularSpecular", "MacroNormal", "FaceCriticalAzimuth", "HairAngularLobe", "Rim", "Outline"],
        "status": "InitialDiffuseSeed_Uncalibrated", "runtime_profile_mutation": "NotUsed"})
    return profile

def expression(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)

def param(mat, cls, name, value, x, y):
    n = expression(mat, cls, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", value)
    return n

def connect(a, b, pin, output_name=""):
    normalized = pin.replace(" ", "").lower()
    names = [str(n) for n in mel.get_material_expression_input_names(b)]
    match = [n for n in names if n.replace(" ", "").lower() == normalized]
    require(len(match) == 1 and mel.connect_material_expressions(a, output_name, b, match[0]), "Cannot connect " + pin)

def parent(style_id, profile, masked):
    folder = "/Game/NPR/Styles/" + style_id + "/Materials"
    mat, initialize = create(folder, "M_S58_Character_Initial", unreal.Material, unreal.MaterialFactoryNew)
    if initialize:
        mel.delete_all_material_expressions(mat)
        mat.set_editor_property("two_sided", masked)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED if masked else unreal.BlendMode.BLEND_OPAQUE)
        texture = expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -200)
        texture.set_editor_property("parameter_name", "BaseColorTexture")
        texture.set_editor_property("texture", assets.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
        tint = param(mat, unreal.MaterialExpressionVectorParameter, "BaseColorTint", unreal.LinearColor(1,1,1,1), -700, 0)
        multiply = expression(mat, unreal.MaterialExpressionMultiply, -400, -150)
        connect(texture, multiply, "A")
        connect(tint, multiply, "B")
        front = expression(mat, unreal.MaterialExpressionSubstrateToonBSDF, -100, 0)
        front.set_editor_property("toon_profile", profile)
        connect(multiply, front, "BaseColor")
        connect(param(mat, unreal.MaterialExpressionScalarParameter, "Roughness", 0.65, -700, 180), front, "Roughness")
        connect(param(mat, unreal.MaterialExpressionScalarParameter, "Specular", 0.35, -700, 280), front, "Specular")
        if masked:
            alpha = expression(mat, unreal.MaterialExpressionMultiply, -400, 400)
            connect(texture, alpha, "A", "A")
            connect(param(mat, unreal.MaterialExpressionScalarParameter, "OpacityScale", 1.0, -700, 450), alpha, "B")
            require(mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY_MASK), "Opacity connection failed")
        require(mel.connect_material_property(front, "", unreal.MaterialProperty.MP_FRONT_MATERIAL), "Front connection failed")
        errors = mel.recompile_material(mat)
        report["compile_requests"].append({"asset": mat.get_path_name(), "immediate_errors": [str(e) for e in (errors or [])]})
        require(not errors, "Material compile errors")
        complete(mat)
    return mat

def instance(character, index, style_id, parent_mat, texture, tint=None, opacity=1.0):
    folder = "/Game/NPR/Characters/" + character + "/Materials/" + style_id
    obj, initialize = create(folder, "MI_%s_%02d_Initial" % (character, index),
                             unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew)
    if initialize:
        mel.set_material_instance_parent(obj, parent_mat)
        mel.set_material_instance_texture_parameter_value(obj, "BaseColorTexture", texture)
        actual = mel.get_material_instance_texture_parameter_value(obj, "BaseColorTexture")
        require(actual and actual.get_path_name() == texture.get_path_name(), "Texture readback failed")
        if tint:
            mel.set_material_instance_vector_parameter_value(obj, "BaseColorTint", unreal.LinearColor(*tint))
        if style_id == "AnimeFlat":
            mel.set_material_instance_scalar_parameter_value(obj, "OpacityScale", opacity)
            require(abs(mel.get_material_instance_scalar_parameter_value(obj, "OpacityScale") - opacity) < 0.0001, "Opacity readback failed")
        assets.set_metadata_tag(obj, "NPRSemanticStyleId", style_id)
        complete(obj)
    return obj

def actor(key, cls, location, rotation=None):
    tag = "PNPR_TwoStyle_" + key
    matches = [a for a in actors.get_all_level_actors() if tag in [str(t) for t in a.get_editor_property("tags")]]
    require(len(matches) <= 1, "Duplicate actor tag")
    if matches:
        obj = matches[0]
        require(isinstance(obj, cls), "Wrong actor class")
        initialize = "PNPR_TwoStyle_Ready" not in [str(t) for t in obj.get_editor_property("tags")]
    else:
        obj = actors.spawn_actor_from_class(cls, unreal.Vector(*location), rotation or unreal.Rotator(), False)
        obj.set_actor_label("PNPR_" + key)
        obj.set_editor_property("tags", [unreal.Name(tag)])
        initialize = True
    report["actors"].append({"label": obj.get_actor_label(), "path": obj.get_path_name()})
    return obj, initialize

def ready_actor(obj):
    tags = list(obj.get_editor_property("tags"))
    if "PNPR_TwoStyle_Ready" not in [str(t) for t in tags]:
        obj.set_editor_property("tags", tags + [unreal.Name("PNPR_TwoStyle_Ready")])

def main():
    actual = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    require(os.path.normcase(str(actual)) == os.path.normcase(str(PROJECT)), "Wrong project")
    imported = json.loads((PROJECT / "Documentation/Assets/Wanderer.UE58.import.json").read_text(encoding="utf-8"))
    require(imported["status"] == "ImportedAndMetadataChecked_VisualPending", "Wanderer import incomplete")
    conversion = json.loads((PROJECT / "Documentation/Assets/Wanderer.Conversion.json").read_text(encoding="utf-8"))
    profiles = {style: style_profile(style, label) for style, label in (("SemiPBRStylized", "EndfieldLike"), ("AnimeFlat", "GenshinLike"))}
    parents = {style: parent(style, profile, style == "AnimeFlat") for style, profile in profiles.items()}
    chen_mesh = assets.load_asset("/Game/EndfieldRender/Character/Qianyu/SKM_Chen")
    chen_creation = json.loads((PROJECT / "Documentation/Assets/ChenQianyu.ProbeCreation.json").read_text(encoding="utf-8"))
    chen_overrides = []
    report["character_slots"]["ChenQianyu"] = []
    for binding in chen_creation["slots"]:
        if binding["probe_policy"] == "Excluded":
            mat = assets.load_asset("/Game/NPR/Characters/ChenQianyu/Materials/M_ExcludedSourceProxy")
        else:
            texture = assets.load_asset(binding["base_color_texture"])
            mat = instance("ChenQianyu", binding["index"], "SemiPBRStylized", parents["SemiPBRStylized"], texture)
        chen_overrides.append(mat)
        report["character_slots"]["ChenQianyu"].append({"index": binding["index"], "material": mat.get_path_name()})
    wanderer_mesh = assets.load_asset(imported["mesh"])
    texture_map = {item["source_relative"]: assets.load_asset(item["asset"]) for item in imported["textures"]}
    material_map = {m["fbx_material_name"]: m for m in conversion["materials"]}
    wanderer_overrides = []
    report["character_slots"]["Wanderer"] = []
    for slot in imported["audit"]["material_slots"]:
        source = material_map[slot["imported_slot_name"]]
        texture = texture_map[source["texture"]]
        opacity = 0.0 if source["index"] == 10 else source["diffuse"][3]
        mat = instance("Wanderer", slot["index"], "AnimeFlat", parents["AnimeFlat"], texture, source["diffuse"], opacity)
        wanderer_overrides.append(mat)
        report["character_slots"]["Wanderer"].append({"index": slot["index"], "pmx_material": source["name"],
            "texture": texture.get_path_name(), "material": mat.get_path_name(), "opacity_scale": opacity,
            "policy": "Exclude PMX additive sphere-map Hair+ overlay" if source["index"] == 10 else "BaseColor and source alpha"})
    if assets.does_asset_exist(MAP):
        world = assets.load_asset(MAP)
        require(assets.get_metadata_tag(world, TAG) == OWNER, "Unowned existing map")
        require(levels.load_level(MAP), "Cannot load map")
    else:
        require(levels.new_level(MAP, False), "Cannot create map")
        assets.set_metadata_tag(editor.get_editor_world(), TAG, OWNER)
        require(levels.save_current_level(), "Map save failed")
    for key, mesh, materials, x, yaw in (("ChenQianyu_EndfieldLike", chen_mesh, chen_overrides, -95, 0),
                                        ("Wanderer_GenshinLike", wanderer_mesh, wanderer_overrides, 95, 90)):
        obj, initialize = actor(key, unreal.SkeletalMeshActor, (x,0,0), unreal.Rotator(pitch=0,yaw=yaw,roll=0))
        if initialize:
            comp = obj.get_component_by_class(unreal.SkeletalMeshComponent)
            comp.set_skeletal_mesh_asset(mesh)
            comp.set_cast_shadow(True)
            for index, mat in enumerate(materials):
                comp.set_material(index, mat)
            ready_actor(obj)
    floor, initialize = actor("TwoStyle_Floor", unreal.StaticMeshActor, (0,0,-4))
    if initialize:
        comp = floor.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_static_mesh(assets.load_asset("/Engine/BasicShapes/Cube"))
        comp.set_material(0, assets.load_asset("/Game/NPR/Materials/M_S58_NativeSlab_Floor"))
        floor.set_actor_scale3d(unreal.Vector(7,5,0.08))
        ready_actor(floor)
    light, initialize = actor("TwoStyle_Key", unreal.DirectionalLight, (0,0,300), unreal.Rotator(pitch=-35,yaw=-100,roll=0))
    if initialize:
        comp = light.get_component_by_class(unreal.DirectionalLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_intensity(3.14159265)
        comp.set_cast_shadows(True)
        comp.set_light_source_angle(0.0)
        ready_actor(light)
    sky, initialize = actor("TwoStyle_Sky", unreal.SkyLight, (0,0,300))
    if initialize:
        comp = sky.get_component_by_class(unreal.SkyLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        comp.set_cubemap(assets.load_asset("/Engine/EngineResources/GrayLightTextureCube"))
        comp.set_intensity(0.25)
        ready_actor(sky)
    pp, initialize = actor("TwoStyle_FixedExposure", unreal.PostProcessVolume, (0,0,0))
    if initialize:
        pp.set_editor_property("unbound", True)
        settings = pp.get_editor_property("settings")
        for name, value in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False),
                ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.0)):
            settings.set_editor_property(name, value)
        pp.set_editor_property("settings", settings)
        ready_actor(pp)
    camera, _ = actor("TwoStyle_Camera", unreal.CameraActor, (0,320,145), unreal.Rotator(pitch=-8,yaw=-90,roll=0))
    ready_actor(camera)
    editor.set_level_viewport_camera_info(unreal.Vector(0,320,145), unreal.Rotator(pitch=-8,yaw=-90,roll=0))
    require(levels.save_current_level(), "Map save failed")
    report["status"] = "Saved_VisualPending"
    report["completed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    flush()
    unreal.log("PNPR_TWO_STYLE_PROBE_SAVED: " + MAP)

try:
    main()
except Exception:
    report["status"] = "Failed"
    report["error"] = traceback.format_exc()
    flush()
    raise
