"""Create the first real UE 5.8 PrincipledNPR probe assets inside Unreal Editor.

Run only after Launcher installation/verification completes. This script is an
asset bootstrap, not a lighting test and not the finished NPR style adapter.
It does not modify the engine, implement Face/Hair, or mutate ToonProfiles at
runtime. Re-running preserves completed owned assets and existing actor edits.
"""

import datetime
import json
import os
import traceback
from pathlib import Path

import unreal


EXPECTED_PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (EXPECTED_PROJECT / "PrincipledNPR.uproject").is_file(), "Wrong project"
assert (PROJECT / "PrincipledNPR.uproject").is_file(), "Wrong project"
REQUIRED_ENGINE_PREFIX = "5.8.3-58210709"
ROOT = "/Game/NPR"
MAP_PATH = ROOT + "/Maps/L_S58_ToonProbe"
OWNER = "PrincipledNPR.Bootstrap.v1"
OWNER_TAG = "PrincipledNPRBootstrapOwner"
READY_TAG = "PrincipledNPRBootstrapReady"
ACTOR_PREFIX = "PNPR_Bootstrap_"
ACTOR_READY_TAG = "PNPR_Bootstrap_SetupComplete"

manifest = {
    "schema_version": 1,
    "bootstrap_status": "Running",
    "rendering_validation": "NotRun",
    "style_profiles": "NotImplemented: these are native diagnostic baselines",
    "face_model": "NotImplemented",
    "hair_model": "NotImplemented",
    "map": MAP_PATH,
    "assets": [],
    "actors": [],
    "material_compile_requests": [],
    "warnings": [],
    "limitations": [
        "Empty immediate compile error arrays do not prove shader completion or visual correctness.",
        "Cast/Receive/Self Shadow, VSM/CSM, Lumen, and indirect specular still require dedicated tests.",
        "Baseline B changes only DiffuseRampIncludeShadow; it is not a second completed StyleProfile.",
    ],
}
manifest_path = None
assets = None
asset_tools = None
levels = None
actors = None
editor = None
mel = unreal.MaterialEditingLibrary


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_manifest():
    if manifest_path is None:
        return
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = manifest_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(str(temporary), str(manifest_path))


def mark_owned(obj):
    assets.set_metadata_tag(obj, OWNER_TAG, OWNER)


def check_owned(obj):
    require(assets.get_metadata_tag(obj, OWNER_TAG) == OWNER,
            "Existing asset is not owned by this bootstrap; refusing to overwrite: " + obj.get_path_name())


def save_asset(obj):
    require(assets.save_loaded_asset(obj, False), "Asset save failed: " + obj.get_path_name())


def create_or_load(name, folder, cls, factory_cls):
    path = folder + "/" + name
    if assets.does_asset_exist(path):
        obj = assets.load_asset(path)
        require(obj is not None and isinstance(obj, cls), "Wrong asset class at " + path)
        check_owned(obj)
        created = False
    else:
        obj = asset_tools.create_asset(name, folder, cls, factory_cls())
        require(obj is not None, "Factory failed to create " + path)
        mark_owned(obj)
        save_asset(obj)
        created = True
    manifest["assets"].append({"path": path, "class": obj.get_class().get_name(),
                               "operation": "Created" if created else "Reused"})
    write_manifest()
    return obj, created


def make_profile(name, include_shadow):
    profile, created = create_or_load(name, ROOT + "/Profiles", unreal.ToonProfile, unreal.ToonProfileFactory)
    ready = assets.get_metadata_tag(profile, READY_TAG) == "1"
    if created or not ready:
        settings = profile.get_editor_property("settings")
        settings.set_editor_property("diffuse_ramp_include_shadow", include_shadow)
        settings.set_editor_property("diffuse_indirect_scale", 1.0)
        settings.set_editor_property("specular_indirect_scale", 1.0)
        settings.set_editor_property("specular_indirect_ramp_repetition", 0.0)
        settings.set_editor_property("diffuse_ramp_offset_strength", 0.0)
        settings.set_editor_property("specular_ramp_offset_strength", 0.0)
        profile.set_editor_property("settings", settings)
        assets.set_metadata_tag(profile, READY_TAG, "1")
        assets.set_metadata_tag(profile, "PrincipledNPRProfileRole", "NativeDiagnosticBaseline_NotStyleProfile")
        save_asset(profile)
    manifest["assets"][-1]["diffuse_ramp_include_shadow"] = bool(
        profile.get_editor_property("settings").get_editor_property("diffuse_ramp_include_shadow"))
    return profile


def expr(material, cls, x, y):
    node = mel.create_material_expression(material, cls, x, y)
    require(node is not None, "Cannot create expression " + cls.__name__)
    return node


def scalar(material, value, x, y):
    node = expr(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", float(value))
    return node


def color(material, value, x, y):
    node = expr(material, unreal.MaterialExpressionConstant3Vector, x, y)
    node.set_editor_property("constant", unreal.LinearColor(value[0], value[1], value[2], 1.0))
    return node


def connect(source, target, semantic_name):
    available = list(mel.get_material_expression_input_names(target))
    normalized = semantic_name.lower().replace(" ", "").replace("_", "")
    matches = [str(n) for n in available
               if str(n).lower().replace(" ", "").replace("_", "") == normalized]
    require(len(matches) == 1, "Cannot identify pin %s among %s" % (semantic_name, available))
    require(mel.connect_material_expressions(source, "", target, matches[0]),
            "Connection failed: " + semantic_name)


def make_material(name, profile=None, surface_color=(0.45, 0.20, 0.08)):
    material, created = create_or_load(name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew)
    ready = assets.get_metadata_tag(material, READY_TAG) == "1"
    front_property = unreal.MaterialProperty.MP_FRONT_MATERIAL
    if created or not ready:
        # Only incomplete assets explicitly owned by this bootstrap may be rebuilt.
        mel.delete_all_material_expressions(material)
        material.set_editor_property("two_sided", False)
        base = color(material, surface_color, -550, -160)
        roughness = scalar(material, 0.45, -550, 100)
        if profile is not None:
            front = expr(material, unreal.MaterialExpressionSubstrateToonBSDF, -180, 0)
            front.set_editor_property("toon_profile", profile)
            connect(base, front, "BaseColor")
            connect(roughness, front, "Roughness")
            connect(scalar(material, 0.5, -550, 200), front, "Specular")
        else:
            front = expr(material, unreal.MaterialExpressionSubstrateSlabBSDF, -180, 0)
            # A physical native Substrate control, without layered/SSS/fuzz inputs.
            connect(base, front, "Diffuse Albedo")
            connect(color(material, (0.04, 0.04, 0.04), -550, -40), front, "F0")
            connect(roughness, front, "Roughness")
        require(mel.connect_material_property(front, "", front_property),
                "Front Material connection failed for " + name)
        errors = mel.recompile_material(material)
        compile_record = {"material": material.get_path_name(), "operation": "Requested",
                          "immediate_errors": [] if errors is None else [str(e) for e in errors],
                          "gpu_validation": "NotRun"}
        manifest["material_compile_requests"].append(compile_record)
        require(not compile_record["immediate_errors"], "Material compile reported errors: " + repr(errors))
        assets.set_metadata_tag(material, READY_TAG, "1")
        save_asset(material)
    else:
        front = mel.get_material_property_input_node(material, front_property)
        expected = unreal.MaterialExpressionSubstrateToonBSDF if profile is not None else unreal.MaterialExpressionSubstrateSlabBSDF
        require(isinstance(front, expected), "Completed material root no longer matches bootstrap: " + name)
        if profile is not None:
            linked = front.get_editor_property("toon_profile")
            require(linked is not None and linked.get_path_name() == profile.get_path_name(),
                    "Completed Toon material uses a different profile: " + name)
        manifest["material_compile_requests"].append({"material": material.get_path_name(),
                                                      "operation": "NotRequested_Reused",
                                                      "gpu_validation": "NotRun"})
    return material


def open_or_create_map():
    if assets.does_asset_exist(MAP_PATH):
        existing = assets.load_asset(MAP_PATH)
        require(existing is not None, "Cannot load existing probe map")
        check_owned(existing)
        require(levels.load_level(MAP_PATH), "Cannot open existing probe map")
    else:
        require(levels.new_level(MAP_PATH, False), "Cannot create non-partitioned probe map")
        world = editor.get_editor_world()
        require(world is not None, "New level has no editor world")
        mark_owned(world)
        require(levels.save_current_level(), "Cannot save owned empty probe map")
    world = editor.get_editor_world()
    require(world is not None and world.get_path_name().startswith(MAP_PATH + "."),
            "Current editor world is not the intended probe map")
    check_owned(world)


def actor(key, cls, location, rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0)):
    tag = ACTOR_PREFIX + key
    matches = [a for a in actors.get_all_level_actors()
               if tag in [str(t) for t in a.get_editor_property("tags")]]
    require(len(matches) <= 1, "Duplicate bootstrap actor tag: " + tag)
    if matches:
        obj = matches[0]
        require(isinstance(obj, cls), "Bootstrap actor has unexpected class: " + tag)
        # Repair incomplete bootstrap setup, while retaining later edits to
        # completed actors. Never remove actors or reset the entire map.
        needs_setup = ACTOR_READY_TAG not in [str(t) for t in obj.get_editor_property("tags")]
        operation = "RepairIncomplete" if needs_setup else "Reused"
    else:
        obj = actors.spawn_actor_from_class(cls, unreal.Vector(*location),
                                           unreal.Rotator(pitch=rotation[0], yaw=rotation[1], roll=rotation[2]), False)
        require(obj is not None, "Failed to spawn actor " + key)
        obj.set_actor_label("PNPR_" + key)
        obj.set_editor_property("tags", [unreal.Name(tag)])
        obj.set_actor_scale3d(unreal.Vector(*scale))
        needs_setup = True
        operation = "Created"
    manifest["actors"].append({"key": key, "actor": obj.get_path_name(),
                               "class": obj.get_class().get_name(),
                               "operation": operation})
    return obj, needs_setup


def actor_setup_complete(obj):
    tags = list(obj.get_editor_property("tags"))
    if ACTOR_READY_TAG not in [str(t) for t in tags]:
        tags.append(unreal.Name(ACTOR_READY_TAG))
        obj.set_editor_property("tags", tags)


def mesh_actor(key, mesh_path, material, location, scale):
    obj, created = actor(key, unreal.StaticMeshActor, location, scale=scale)
    if created:
        component = obj.get_component_by_class(unreal.StaticMeshComponent)
        require(component is not None, "StaticMeshActor missing component")
        mesh = assets.load_asset(mesh_path)
        require(mesh is not None, "Missing engine primitive " + mesh_path)
        component.set_static_mesh(mesh)
        actual_mesh = component.get_editor_property("static_mesh")
        require(actual_mesh is not None and actual_mesh.get_path_name() == mesh.get_path_name(),
                "Static mesh assignment failed")
        component.set_material(0, material)
        component.set_cast_shadow(True)
        actor_setup_complete(obj)
    return obj


def configure_sky():
    sky, created = actor("SkyLight", unreal.SkyLight, (0.0, 0.0, 350.0))
    if created:
        component = sky.get_component_by_class(unreal.SkyLightComponent)
        require(component is not None, "SkyLight component missing")
        cubemap = assets.load_asset("/Engine/EngineResources/GrayLightTextureCube")
        require(cubemap is not None, "Required engine cubemap missing")
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        component.set_cubemap(cubemap)
        component.set_intensity(0.25)
        actor_setup_complete(sky)


def configure_exposure():
    exposure, created = actor("FixedExposure", unreal.PostProcessVolume, (0.0, 0.0, 0.0))
    if created:
        exposure.set_editor_property("unbound", True)
        exposure.set_editor_property("priority", 100.0)
        settings = exposure.get_editor_property("settings")
        settings.set_editor_property("override_auto_exposure_method", True)
        settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
        settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
        settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", 0.0)
        exposure.set_editor_property("settings", settings)
        actor_setup_complete(exposure)


def configure_camera():
    camera, _ = actor("ProbeCamera", unreal.CameraActor, (700.0, -850.0, 500.0), (-20.0, 130.0, 0.0))
    player_start, _ = actor("PlayerStart", unreal.PlayerStart, (650.0, -650.0, 140.0), (0.0, 130.0, 0.0))
    actor_setup_complete(camera)
    actor_setup_complete(player_start)
    editor.set_level_viewport_camera_info(unreal.Vector(700.0, -850.0, 500.0),
                                         unreal.Rotator(pitch=-20.0, yaw=130.0, roll=0.0))


def optional_setup(label, function):
    try:
        function()
    except Exception:
        warning = {"stage": label, "error": traceback.format_exc(), "status": "NeedsReview"}
        manifest["warnings"].append(warning)
        unreal.log_warning("PrincipledNPR optional setup failed (%s): %s" % (label, warning["error"]))


def populate_map(material_a, material_b, lit_material, floor_material):
    mesh_actor("Toon_A_ShadowOutsideRamp", "/Engine/BasicShapes/Sphere", material_a,
               (-240.0, 80.0, 90.0), (1.8, 1.8, 1.8))
    mesh_actor("Toon_B_ShadowInsideRamp", "/Engine/BasicShapes/Sphere", material_b,
               (0.0, 80.0, 90.0), (1.8, 1.8, 1.8))
    mesh_actor("NativeSlab_Control", "/Engine/BasicShapes/Sphere", lit_material,
               (240.0, 80.0, 90.0), (1.8, 1.8, 1.8))
    mesh_actor("Floor", "/Engine/BasicShapes/Cube", floor_material,
               (0.0, 0.0, -7.5), (10.0, 8.0, 0.15))
    mesh_actor("Occluder", "/Engine/BasicShapes/Cube", floor_material,
               (0.0, 250.0, 140.0), (0.9, 0.5, 2.8))

    light, created = actor("KeyDirectional", unreal.DirectionalLight,
                           (0.0, 0.0, 450.0), (-40.0, -100.0, 0.0))
    if created:
        component = light.get_component_by_class(unreal.DirectionalLightComponent)
        require(component is not None, "Directional light component missing")
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        # Unit-gain diagnostic setup; adjust for physical illuminance experiments.
        component.set_intensity(3.14159265)
        component.set_light_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0), False)
        component.set_light_source_angle(0.0)
        component.set_cast_shadows(True)
        actor_setup_complete(light)

    optional_setup("SkyLight", configure_sky)
    optional_setup("FixedExposure", configure_exposure)
    optional_setup("CameraAndPlayerStart", configure_camera)
    require(levels.save_current_level(), "Final probe map save failed")


def main():
    global manifest_path, assets, asset_tools, levels, actors, editor
    project_dir = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    require(os.path.normcase(str(project_dir)) == os.path.normcase(str(EXPECTED_PROJECT.resolve())),
            "Run only in the intended PrincipledNPR project: " + str(project_dir))
    require((project_dir / "PrincipledNPR.uproject").is_file(), "Expected project descriptor missing")
    manifest_path = project_dir / "Saved" / "PrincipledNPR" / "bootstrap_manifest.json"
    manifest["started_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    manifest["project_dir"] = str(project_dir)
    manifest["engine_version"] = unreal.SystemLibrary.get_engine_version()
    write_manifest()
    try:
        require(manifest["engine_version"].startswith(REQUIRED_ENGINE_PREFIX),
                "Wrong engine version/CL: " + manifest["engine_version"])
        for name in ("ToonProfile", "ToonProfileFactory", "MaterialExpressionSubstrateToonBSDF",
                     "MaterialExpressionSubstrateSlabBSDF", "EditorAssetSubsystem",
                     "LevelEditorSubsystem", "EditorActorSubsystem", "UnrealEditorSubsystem"):
            require(hasattr(unreal, name), "Required Unreal Python class missing: " + name)
        expected_cvars = {"r.Substrate": 1, "r.Substrate.ProjectGBufferFormat": 0,
                          "r.ForwardShading": 0, "r.DynamicGlobalIlluminationMethod": 1,
                          "r.ReflectionMethod": 1, "r.Shadow.Virtual.Enable": 1}
        manifest["render_settings"] = {}
        for name, expected in expected_cvars.items():
            value = unreal.SystemLibrary.get_console_variable_int_value(name)
            manifest["render_settings"][name] = value
            require(value == expected, "Required setting mismatch: %s = %s, expected %s" % (name, value, expected))
        assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
        levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        for folder in ("Maps", "Profiles", "Materials", "Presets", "TestAssets"):
            path = ROOT + "/" + folder
            if not assets.does_directory_exist(path):
                require(assets.make_directory(path), "Cannot create content folder " + folder)
        profile_a = make_profile("TP_S58_NativeBaseline_A", False)
        profile_b = make_profile("TP_S58_NativeBaseline_B", True)
        material_a = make_material("M_S58_ToonBaseline_A", profile_a)
        material_b = make_material("M_S58_ToonBaseline_B", profile_b)
        lit = make_material("M_S58_NativeSlab_Control")
        floor = make_material("M_S58_NativeSlab_Floor", surface_color=(0.18, 0.18, 0.18))
        open_or_create_map()
        populate_map(material_a, material_b, lit, floor)
        require(assets.does_asset_exist(MAP_PATH), "Saved probe map was not registered")
        map_file = project_dir / "Content" / "NPR" / "Maps" / "L_S58_ToonProbe.umap"
        require(map_file.is_file(), "Saved map file missing from disk")
        manifest["map_file"] = str(map_file)
        manifest["bootstrap_status"] = ("AssetsCreatedAndSaved_WithWarnings_RenderingNotValidated"
                                        if manifest["warnings"] else "AssetsCreatedAndSaved_RenderingNotValidated")
        unreal.log("PrincipledNPR bootstrap saved its probe assets. Rendering tests remain NotRun.")
    except Exception:
        manifest["bootstrap_status"] = "Failed"
        manifest["error"] = traceback.format_exc()
        unreal.log_error(manifest["error"])
        raise
    finally:
        manifest["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_manifest()


if __name__ == "__main__":
    main()
