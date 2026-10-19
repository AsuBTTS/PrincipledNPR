"""Create an owned one-character, one-opaque-slot Outline workflow fixture.
Editor-only developer utility. Never edits imported source assets or existing probe maps.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / "PrincipledNPR.uproject").is_file(), "Wrong project"
A = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
L = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
M = unreal.MaterialEditingLibrary
MAP = "/Game/NPR/Maps/L_Outline_Workflow"
OWNER = "PrincipledNPR.OutlineFixture.v1"
SOURCE = "/Game/NPR/Outline/Fixture/M_Cloth_PBR_Source"
REPORT = {"map": MAP, "owner": OWNER, "slot": 2, "status": "Running"}


def expression(mat, cls, **props):
    obj = M.create_material_expression(mat, cls)
    for key, value in props.items():
        obj.set_editor_property(key, value)
    return obj


try:
    if A.does_asset_exist(MAP):
        assert L.load_level(MAP)
        found = [x for x in ACTORS.get_all_level_actors() if OWNER in [str(t) for t in x.tags]]
        assert len(found) == 1, "Existing map is not the owned fixture; refuse overwrite"
        ACTORS.set_selected_level_actors(found)
        REPORT["status"] = "ReusedExistingFixture"
    else:
        if A.does_asset_exist(SOURCE):
            source = A.load_asset(SOURCE)
            assert A.get_metadata_tag(source, "NPROutlineOwner") == OWNER
        else:
            source = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "M_Cloth_PBR_Source", "/Game/NPR/Outline/Fixture", unreal.Material, unreal.MaterialFactoryNew())
            base = expression(source, unreal.MaterialExpressionTextureSampleParameter2D,
                              parameter_name="BaseColorTexture", texture=A.load_asset(
                                  "/Game/EndfieldRender/Character/Qianyu/Tex/T_actor_chen_cloth_01_D"))
            normal = expression(source, unreal.MaterialExpressionTextureSampleParameter2D,
                                parameter_name="NormalTexture", texture=A.load_asset(
                                    "/Engine/EngineMaterials/DefaultNormal"),
                                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
            slab = expression(source, unreal.MaterialExpressionSubstrateSlabBSDF)
            names = M.get_material_expression_input_names(slab)
            for src, token in [(base, "diffusealbedo"), (normal, "normal")]:
                pin = [str(x) for x in names if str(x).replace(" ", "").lower() == token]
                assert len(pin) == 1 and M.connect_material_expressions(src, "", slab, pin[0]), token
            assert M.connect_material_property(slab, "", unreal.MaterialProperty.MP_FRONT_MATERIAL)
            M.set_material_usage(source, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
            M.layout_material_expressions(source)
            assert not M.recompile_material(source)
            A.set_metadata_tag(source, "NPROutlineOwner", OWNER)
            assert A.save_loaded_asset(source, False)
        assert L.new_level(MAP, False)
        actor = ACTORS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
        actor.set_actor_label("Outline_ControlledCharacter_Slot02")
        actor.tags = [unreal.Name(OWNER)]
        mesh = actor.get_component_by_class(unreal.SkeletalMeshComponent)
        mesh.set_skeletal_mesh_asset(A.load_asset("/Game/EndfieldRender/Character/Qianyu/SKM_Chen"))
        for index in range(mesh.get_num_materials()):
            path = "/Game/NPR/Characters/ChenQianyu/Materials/SemiPBRStylized/MI_ChenQianyu_%02d_Initial" % index
            mat = A.load_asset(path) if A.does_asset_exist(path) else A.load_asset(
                "/Game/NPR/Characters/ChenQianyu/Materials/M_ExcludedSourceProxy")
            assert mat
            mesh.set_material(index, mat)
        mesh.set_material(2, source)
        light = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 300),
                                             unreal.Rotator(pitch=-35, yaw=-100, roll=0))
        light.set_actor_label("Outline_KeyLight")
        component = light.get_component_by_class(unreal.DirectionalLightComponent)
        component.set_intensity(3.14159)
        sky = ACTORS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300))
        sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
        sky_component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        sky_component.set_cubemap(A.load_asset("/Engine/MapTemplates/Sky/DaylightAmbientCubemap"))
        sky_component.set_intensity(0.6)
        post = ACTORS.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
        post.set_editor_property("unbound", True)
        settings = post.get_editor_property("settings")
        for key, value in [("override_auto_exposure_method", True),
                           ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                           ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.0),
                           ("override_auto_exposure_apply_physical_camera_exposure", True),
                           ("auto_exposure_apply_physical_camera_exposure", False)]:
            settings.set_editor_property(key, value)
        post.set_editor_property("settings", settings)
        camera = ACTORS.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 280, 125),
                                              unreal.Rotator(pitch=-8, yaw=-90, roll=0))
        camera.set_actor_label("Outline_FrontCamera")
        ED.set_level_viewport_camera_info(unreal.Vector(0, 280, 125), unreal.Rotator(pitch=-8, yaw=-90, roll=0))
        assert L.save_current_level()
        ACTORS.set_selected_level_actors([actor])
        REPORT["status"] = "CreatedAndSaved"
    REPORT["surface_scope"] = "Opaque physical Substrate Slab on slot 2, original cloth BaseColor and known neutral Normal; no imported packed-normal encoding is guessed or modified"
finally:
    path = ROOT / "Saved/NPR/Outline/FixtureCreation.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
