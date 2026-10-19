"""Real-RHI integration matrix on an owned scene. Reopen is a separate editor process.
NPR_MULTI_STAGE=Create|Reopen. Does not edit imported character assets.
"""
import json
import os
import runpy
import traceback
import time
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
STAGE = os.environ.get("NPR_MULTI_STAGE", "Create")
OUT = ROOT / os.environ.get("NPR_VALIDATION_OUTPUT", "Documentation/Outline/MultiScope/Validation/" + STAGE)
OUT.mkdir(parents=True, exist_ok=True)
MAP = "/Game/NPR/Maps/L_Outline_MultiScope_v1"
FOLDER = "/Game/NPR/Outline/MultiScope"
OWNER = "PrincipledNPR.MultiScope.v1"
REPORT = {"stage": STAGE, "status": "Running", "checks": [], "captures": [],
          "visual_acceptance": "Pending", "limitations": []}
A = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
L = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
SM = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
LIB = unreal.NPROutlineEditorLibrary
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def flush():
    target = OUT / "Results.json"
    temporary = OUT / "Results.pending.json"
    for attempt in range(20):
        try:
            temporary.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
            temporary.replace(target)
            return
        except OSError as error:
            if attempt == 19 or error.errno not in (13, 22):
                raise
            time.sleep(0.05)

def check(name, passed, detail=None):
    REPORT["checks"].append({"name": name, "passed": bool(passed), "detail": detail})
    flush()
    assert passed, name + ": " + str(detail)

def settings(method=unreal.NPROutlineMethod.SCREEN_SPACE, rgb=(0.8, 0.01, 0.01), width=4):
    s = unreal.NPROutlineSettings()
    s.method = method
    s.color = unreal.LinearColor(*rgb, 1)
    s.width_pixels = width
    s.hull_width_cm = 1.5
    return s

def resolved(component):
    value = component.resolve_regions()
    assert value is not None, component.get_last_error()
    return value[0] if isinstance(value, tuple) else value

def mapping(mesh):
    value = unreal.NPROutlineActorComponent.get_section_mapping(mesh)
    assert value is not None
    return value[0] if isinstance(value, tuple) else value

def actor_mesh(label, asset, pos=(0, 0, 0)):
    actor = ACT.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*pos))
    actor.set_actor_label(label)
    actor.tags = [unreal.Name(OWNER)]
    mesh = actor.get_component_by_class(unreal.StaticMeshComponent)
    mesh.set_static_mesh(asset)
    for slot in range(mesh.get_num_materials()):
        mesh.set_material(slot, solid)
    return actor, mesh

def configure(actor):
    result = LIB.configure_actor(actor)
    component = result[0] if isinstance(result, tuple) else result
    assert component, result
    component.base_settings = settings()
    check("Refresh_" + actor.get_actor_label(), component.refresh_outline(), component.get_last_error())
    return component

def material_rule(name, color=None, disable=False):
    rule = unreal.NPROutlineMaterialRule()
    rule.material_slot_name = name
    p = unreal.NPROutlineOverride()
    if color:
        p.override_color = True
        p.values = settings(rgb=color)
    if disable:
        p.override_enabled = True
        p.enabled = False
    rule.override = p
    return rule

def build_assets():
    # Two intersecting boxes with explicit material groups, imported into ONE static mesh.
    src = ROOT / "Saved/NPR/Outline/MultiScope/TwoRegions.obj"
    src.parent.mkdir(parents=True, exist_ok=True)
    lines = ["mtllib TwoRegions.mtl"]
    for i, center in enumerate((-30, 30)):
        lines += ["o Part" + str(i), "usemtl Region" + str(i)]
        for x, y, z in [(-40,-35,-40),(40,-35,-40),(40,35,-40),(-40,35,-40),
                        (-40,-35,40),(40,-35,40),(40,35,40),(-40,35,40)]:
            lines.append("v %s %s %s" % (x+center, y+i*12, z))
        for face in [(1,4,3,2),(5,6,7,8),(1,2,6,5),(2,3,7,6),(3,4,8,7),(4,1,5,8)]:
            lines.append("f " + " ".join(str(v+i*8) for v in face))
    src.write_text("\n".join(lines), encoding="utf-8")
    src.with_suffix(".mtl").write_text("newmtl Region0\nKd 0.6 0.6 0.6\nnewmtl Region1\nKd 0.6 0.6 0.6\n", encoding="utf-8")
    path = FOLDER + "/SM_TwoRegions"
    if not A.does_asset_exist(path):
        task = unreal.AssetImportTask()
        task.filename = str(src)
        task.destination_path = FOLDER
        task.destination_name = "SM_TwoRegions"
        task.automated = True
        task.save = True
        task.factory = unreal.FbxFactory()
        options = unreal.FbxImportUI()
        options.import_mesh = True
        options.import_as_skeletal = False
        options.import_materials = False
        options.import_textures = False
        options.static_mesh_import_data.combine_meshes = True
        options.static_mesh_import_data.auto_generate_collision = False
        task.options = options
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        obj = A.load_asset(path)
        assert obj, task.imported_object_paths
        A.set_metadata_tag(obj, "NPROutlineOwner", OWNER)
        A.save_loaded_asset(obj)
    two = A.load_asset(path)
    assert A.get_metadata_tag(two, "NPROutlineOwner") == OWNER
    shared_path = FOLDER + "/SM_SharedSlot"
    if not A.does_asset_exist(shared_path):
        shared = A.duplicate_asset(path, shared_path)
        SM.set_lod_material_slot(shared, 0, 0, 1)
        A.save_loaded_asset(shared)
    lod_path = FOLDER + "/SM_TwoLODs"
    if not A.does_asset_exist(lod_path):
        lod = A.duplicate_asset(path, lod_path)
        options = unreal.StaticMeshReductionOptions()
        r0 = unreal.StaticMeshReductionSettings(percent_triangles=1.0, screen_size=1.0)
        r1 = unreal.StaticMeshReductionSettings(percent_triangles=0.8, screen_size=0.25)
        options.reduction_settings = [r0, r1]
        SM.set_lods(lod, options)
        A.save_loaded_asset(lod)
    return two, A.load_asset(shared_path), A.load_asset(lod_path)

def solid_material():
    path = FOLDER + "/M_ValidationSolid"
    if A.does_asset_exist(path):
        return A.load_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_ValidationSolid", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    node.set_editor_property("constant", unreal.LinearColor(0.2, 0.2, 0.2, 1))
    assert unreal.MaterialEditingLibrary.connect_material_property(node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    assert not unreal.MaterialEditingLibrary.recompile_material(mat)
    A.set_metadata_tag(mat, "NPROutlineOwner", OWNER)
    A.save_loaded_asset(mat)
    return mat

steps = []
callback = None
delay = 25
capture_component = None

def refresh_group(scope, split=False):
    group.scope = scope
    rules = list(group.meshes)
    for i, rule in enumerate(rules):
        p = unreal.NPROutlineOverride()
        if split and i == 1:
            p.override_color = True
            p.values = settings(rgb=(0.01, 0.8, 0.01))
        rule.override = p
    group.meshes = rules
    check("GroupRefresh_" + str(scope) + str(split), group.refresh_outline(), group.get_last_error())

def capture(name):
    capture_component.capture_scene()
    unreal.RenderingLibrary.export_render_target(world, render_target, str(OUT), name + ".png")
    check("Capture_" + name, (OUT / (name + ".png")).is_file())
    REPORT["captures"].append(name + ".png")

def finish(failed=False):
    REPORT["status"] = "Failed" if failed else "DataPassed_ImagesRequireReview"
    flush()
    if not os.environ.get("NPR_VALIDATION_SESSION"):
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)

try:
    check("RealRHI", "nullrhi" not in unreal.SystemLibrary.get_command_line().lower())
    if STAGE == "Reopen":
        check("LoadSavedScene", L.load_level(MAP))
        groups = [a for a in ACT.get_all_level_actors() if a.get_actor_label() == "MultiScope_CharacterGroup"]
        check("GroupPersists", len(groups) == 1)
        group = groups[0].get_component_by_class(unreal.NPROutlineActorComponent)
        check("ConfigPersists", group is not None and len(group.meshes) == 2)
        check("RebuildAfterReopen", group.refresh_outline(), group.get_last_error())
        helpers = group.get_outline_meshes()
        check("ReopenedUnifiedGroup", len(helpers) == 2 and group.get_screen_pass_count() == 1 and
              len(set(h.custom_depth_stencil_value for h in helpers)) == 1)
        check("ReopenedPreset", group.preset is not None and group.base_settings.width_pixels == 4)
        modular = [a for a in ACT.get_all_level_actors() if a.get_actor_label() == "MultiScope_Skeletal"][0]
        modular_group = modular.get_component_by_class(unreal.NPROutlineActorComponent)
        check("ModularSkeletalComponentsPersist", len(modular_group.meshes) == 2 and modular_group.refresh_outline() and len(modular_group.get_outline_meshes()) == 2)
        ACT.set_selected_level_actors([modular])
        LIB.open_editor()
        check("GroupPreviewCopiesBothComponents", LIB.get_comparison_mesh_count(False) == 2 and LIB.get_comparison_mesh_count(True) == 2)
        check("GroupPreviewCaptureSize", LIB.set_comparison_capture_size(420, 560))
        preview_settings = modular_group.base_settings
        def preview_export(name):
            folder = OUT / name
            check("Export_" + name, LIB.export_comparison(str(folder)))
            REPORT["captures"].extend([name + "/Original.png", name + "/Outline.png"])
        def preview_change(value):
            modular_group.base_settings = value
            check("PreviewLiveSettings_" + str(value.method), modular_group.refresh_outline(), modular_group.get_last_error())
        steps += [(lambda: preview_export("GroupPreview"), 5),
                  (lambda: preview_change(settings(unreal.NPROutlineMethod.NONE)), 15),
                  (lambda: preview_export("GroupPreviewNone"), 5),
                  (lambda: preview_change(settings(rgb=(0.01, 0.8, 0.01), width=6)), 15),
                  (lambda: preview_export("GroupPreviewGreen"), 5),
                  (lambda: preview_change(preview_settings), 10)]
        def textured_demo():
            runpy.run_path(str(ROOT / "Tools/open_outline_character_demo.py"))
            check("TexturedCharacterPreviewGeometry", LIB.get_comparison_mesh_count(False) == 1 and LIB.get_comparison_mesh_count(True) == 1)
            LIB.set_comparison_capture_size(420, 560)
        steps += [(textured_demo, 25), (lambda: preview_export("TexturedCharacter"), 5)]
    else:
        runpy.run_path(str(ROOT / "Tools/create_outline_backend_assets.py"))
        runpy.run_path(str(ROOT / "Tools/audit_outline_assets.py"))
        check("ShadersCompiled", LIB.finish_backend_compilation() == "")
        if A.does_asset_exist(MAP):
            assert L.load_level(MAP)
            assert A.get_metadata_tag(A.load_asset(MAP), "NPROutlineOwner") == OWNER, "Refuse to reset an unowned validation map"
            for old in ACT.get_all_level_actors():
                if OWNER in [str(t) for t in old.tags]:
                    ACT.destroy_actor(old)
            check("ReuseOwnedLevel", True)
        else:
            check("NewOwnedLevel", L.new_level(MAP, False))
            A.set_metadata_tag(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), "NPROutlineOwner", OWNER)
            assert L.save_current_level()
        solid = solid_material()
        LIB.finish_backend_compilation()
        two, shared, lod = build_assets()
        actor, mesh = actor_mesh("MultiScope_Regions", two)
        component = configure(actor)
        rows = mapping(mesh)
        check("OneMeshMultiSectionAndSlot", len(rows) == 2 and len(set(r.material_slot_index for r in rows)) == 2)
        rules = list(component.meshes)
        rule = rules[0]
        rule.materials = [material_rule(rows[1].material_slot_name, (0.01, 0.8, 0.01))]
        rules[0] = rule
        component.meshes = rules
        check("TwoMaterialStyles", component.refresh_outline() and component.get_screen_pass_count() == 2)
        section = unreal.NPROutlineSectionRule()
        section.section_index = 1
        p = unreal.NPROutlineOverride()
        p.override_width = True
        p.values = settings(width=7)
        section.override = p
        rule.sections = [section]
        component.meshes = [rule]
        check("SectionOverridesMaterialWidth", component.refresh_outline() and
              any(r.material_slot_index == rows[1].material_slot_index and r.settings.width_pixels == 7 and
                  r.settings.color.g > 0.7 for r in resolved(component)))
        rule.materials = [material_rule(rows[1].material_slot_name, disable=True)]
        rule.sections = []
        component.meshes = [rule]
        check("LocalSlotDisable", component.refresh_outline() and sum(r.enabled for r in resolved(component)) == 1)
        shared_actor, shared_mesh = actor_mesh("MultiScope_SharedSlot", shared, (350, 0, 0))
        shared_component = configure(shared_actor)
        check("SharedSlotMapping", len(mapping(shared_mesh)) == 2 and len(set(r.material_slot_index for r in mapping(shared_mesh))) == 1)
        shared_rule = list(shared_component.meshes)[0]
        shared_rule.sections = [section]
        shared_component.meshes = [shared_rule]
        check("SharedSlotSectionRejected", not shared_component.refresh_outline() and "shares" in shared_component.get_last_error(), shared_component.get_last_error())
        shared_rule.sections = []
        shared_component.meshes = [shared_rule]
        check("SharedSlotUsesOneStyle", shared_component.refresh_outline() and shared_component.get_screen_pass_count() == 1)
        lod_actor, lod_mesh = actor_mesh("MultiScope_LOD", lod, (700, 0, 0))
        lod_component = configure(lod_actor)
        check("MultipleLODsMapped", max(r.lod_index for r in mapping(lod_mesh)) >= 1)
        lod_mesh.set_forced_lod_model(2)
        lod_component.refresh_outline()
        check("StaticForcedLODSynced", all(h.get_editor_property("forced_lod_model") == 2 for h in lod_component.get_outline_meshes()))
        lod_rule = list(lod_component.meshes)[0]
        lod_rule.sections = [section]
        lod_component.meshes = [lod_rule]
        check("MultiLODSectionRejected", not lod_component.refresh_outline() and "multi-LOD" in lod_component.get_last_error())
        lod_rule.sections = []
        lod_component.meshes = [lod_rule]
        lod_component.refresh_outline()

        # Multi-component actor: intersecting opaque spheres. Same stencil must suppress the join.
        group_actor, sphere = actor_mesh("MultiScope_CharacterGroup", A.load_asset("/Engine/BasicShapes/Sphere"), (0, 450, 0))
        second = LIB.add_validation_static_mesh(group_actor, A.load_asset("/Engine/BasicShapes/Sphere"),
                    unreal.Transform(location=unreal.Vector(60, 20, 0)))
        second.set_material(0, solid)
        group = configure(group_actor)
        check("CollectMultipleComponents", len(group.meshes) == 2)
        refresh_group(unreal.NPROutlineScope.UNIFIED_SILHOUETTE)
        helpers = group.get_outline_meshes()
        check("UnifiedOneStencilOnePass", len(helpers) == 2 and group.get_screen_pass_count() == 1 and
              len(set(h.custom_depth_stencil_value for h in helpers)) == 1)
        check("SourceCustomDepthUnchanged", not sphere.render_custom_depth and not second.render_custom_depth)
        old_materials = [sphere.get_material(0), second.get_material(0)]
        group.base_settings = settings(unreal.NPROutlineMethod.INVERTED_HULL)
        check("UnifiedHullRejected", not group.refresh_outline() and "requires Screen" in group.get_last_error())
        group.base_settings = settings()
        refresh_group(unreal.NPROutlineScope.INDEPENDENT_REGIONS, True)
        check("IndependentPassesAndIDs", group.get_screen_pass_count() == 2 and
              len(set(h.custom_depth_stencil_value for h in group.get_outline_meshes())) == 2)
        group.scope = unreal.NPROutlineScope.UNIFIED_SILHOUETTE
        check("UnifiedLocalStyleRejected", not group.refresh_outline() and "one style" in group.get_last_error())
        refresh_group(unreal.NPROutlineScope.INDEPENDENT_REGIONS)
        rules = list(group.meshes)
        p = unreal.NPROutlineOverride(override_enabled=True, enabled=False)
        rules[1].override = p
        group.meshes = rules
        check("ComponentDisable", group.refresh_outline() and len(group.get_outline_meshes()) == 1)
        refresh_group(unreal.NPROutlineScope.INDEPENDENT_REGIONS)
        group.base_settings = settings(unreal.NPROutlineMethod.INVERTED_HULL)
        check("MultiComponentHull", group.refresh_outline() and len(group.get_outline_meshes()) == 2 and group.get_screen_pass_count() == 0)
        s = group.base_settings
        s.use_vertex_color_width = True
        group.base_settings = s
        check("VertexWidthMIDBinding", group.refresh_outline() and all(
              h.get_material(0).get_scalar_parameter_value("UseVertexColorWidth") == 1 for h in group.get_outline_meshes()))
        group.base_settings = settings()
        refresh_group(unreal.NPROutlineScope.UNIFIED_SILHOUETTE)
        check("SourceMaterialsUnchanged", old_materials == [sphere.get_material(0), second.get_material(0)])

        # Real imported skeletal resource, with opaque override only on this test component.
        sk_actor = ACT.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(1100, 0, 0))
        sk_actor.set_actor_label("MultiScope_Skeletal")
        sk_actor.tags = [unreal.Name(OWNER)]
        skel = sk_actor.get_component_by_class(unreal.SkeletalMeshComponent)
        skel.set_skeletal_mesh_asset(A.load_asset("/Game/EndfieldRender/Character/Qianyu/SKM_Chen"))
        for i in range(skel.get_num_materials()):
            skel.set_material(i, solid)
        second_skel = LIB.add_validation_skeletal_mesh(sk_actor, A.load_asset("/Game/EndfieldRender/Character/Qianyu/SKM_Chen"))
        for i in range(second_skel.get_num_materials()):
            second_skel.set_material(i, solid)
        sk_rows = mapping(skel)
        # Split the imported character into complementary material regions on two components.
        # The source mesh asset itself remains untouched.
        for row in sk_rows:
            first_part = row.material_slot_index < skel.get_num_materials() // 2
            skel.show_material_section(row.material_slot_index, row.section_index, first_part, row.lod_index)
            second_skel.show_material_section(row.material_slot_index, row.section_index, not first_part, row.lod_index)
        sk_group = configure(sk_actor)
        sk_group.scope = unreal.NPROutlineScope.UNIFIED_SILHOUETTE
        sk_group.refresh_outline()
        check("ModularCharacterTwoSkeletalComponents", len(sk_group.meshes) == 2 and len(sk_group.get_outline_meshes()) == 2 and sk_group.get_screen_pass_count() == 1)
        check("SkeletalLeaderPose", all(h.get_editor_property("leader_pose_component") in (skel, second_skel) for h in sk_group.get_outline_meshes()))
        check("SkeletalSectionMapping", len(mapping(skel)) > 1)
        skel.set_forced_lod(1)
        second_skel.set_forced_lod(1)
        sk_group.refresh_outline()
        check("SkeletalForcedLODSynced", all(h.get_forced_lod() == 1 for h in sk_group.get_outline_meshes()))
        anim = A.load_asset("/Game/EndfieldRender/Character/Qianyu/SKM_Qianyu_Anim")
        REPORT["animation_asset_class"] = anim.get_class().get_name() if anim else None
        if isinstance(anim, unreal.AnimSequence):
            bones = [skel.get_bone_name(i) for i in range(skel.get_num_bones())]
            skel.play_animation(anim, True)
            second_skel.play_animation(anim, True)
            skel.set_update_animation_in_editor(True)
            LIB.sample_validation_animation(skel, 0)
            LIB.sample_validation_animation(second_skel, 0)
            before = [skel.get_socket_location(b) for b in bones]
            def animation_check():
                LIB.sample_validation_animation(skel, min(0.8, anim.get_play_length() * 0.7))
                LIB.sample_validation_animation(second_skel, min(0.8, anim.get_play_length() * 0.7))
                after = [skel.get_socket_location(b) for b in bones]
                movement = max((a - b).length() for a, b in zip(after, before))
                helpers = sk_group.get_outline_meshes()
                error = max((h.get_socket_location(b) - h.get_editor_property("leader_pose_component").get_socket_location(b)).length()
                            for h in helpers for b in bones)
                check("AnimatedBonesMove", movement > 0.01, movement)
                check("AnimatedHelperBoneTransformsMatch", error < 0.01, error)
            steps.append((animation_check, 5))
        else:
            REPORT["limitations"].append("No confirmed compatible animation clip; animation deformation is not accepted.")

        # Isolated scene-capture view of the intersecting component group.
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        cap_actor = ACT.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(20, 150, 10), unreal.Rotator(pitch=0, yaw=90, roll=0))
        cap_actor.tags = [unreal.Name(OWNER)]
        capture_component = cap_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        render_target = unreal.RenderingLibrary.create_render_target2d(world, 512, 512, unreal.TextureRenderTargetFormat.RTF_RGBA8)
        capture_component.texture_target = render_target
        capture_component.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        capture_component.capture_every_frame = False
        capture_component.capture_on_movement = False
        capture_component.always_persist_rendering_state = True
        REPORT["capture_transform"] = str(capture_component.get_world_transform())
        pp = capture_component.post_process_settings
        pp.override_auto_exposure_method = True
        pp.auto_exposure_method = unreal.AutoExposureMethod.AEM_MANUAL
        pp.override_auto_exposure_bias = True
        pp.auto_exposure_bias = 0
        pp.override_auto_exposure_apply_physical_camera_exposure = True
        pp.auto_exposure_apply_physical_camera_exposure = False
        capture_component.post_process_settings = pp
        occluder, occluder_mesh = actor_mesh("MultiScope_Occluder", A.load_asset("/Engine/BasicShapes/Cube"), (20, 280, 0))
        occluder.set_actor_scale3d(unreal.Vector(0.25, 0.5, 1.2))
        occluder_mesh.set_visibility(False)
        def enabled(value):
            group.enabled = value
            group.refresh_outline()
        steps += [(lambda: enabled(False), 10),
                  (lambda: capture("NoOutline"), 5),
                  (lambda: enabled(True), 10),
                  (lambda: capture("Unified"), 10),
                  (lambda: refresh_group(unreal.NPROutlineScope.INDEPENDENT_REGIONS, True), 15),
                  (lambda: capture("Independent"), 10),
                  (lambda: refresh_group(unreal.NPROutlineScope.UNIFIED_SILHOUETTE), 10),
                  (lambda: occluder_mesh.set_visibility(True), 10),
                  (lambda: capture("Occluded"), 10),
                  (lambda: occluder_mesh.set_visibility(False), 5)]
        def save():
            global capture_component
            preset_result = LIB.save_preset(FOLDER + "/OP_ActorBase", group.base_settings)
            preset = preset_result[0] if isinstance(preset_result, tuple) else preset_result
            check("SaveBasePreset", preset is not None)
            group.apply_preset(preset)
            cap_actor.destroy_actor()
            capture_component = None
            check("SaveScene", LIB.save_actor_outline(group) is not None)
        steps.append((save, 1))

    def tick(delta):
        global delay
        try:
            if capture_component:
                capture_component.capture_scene()
            if delay:
                delay -= 1
                return
            if steps:
                action, delay = steps.pop(0)
                action()
            else:
                unreal.unregister_slate_post_tick_callback(callback)
                finish()
        except Exception:
            REPORT["exception"] = traceback.format_exc()
            unreal.unregister_slate_post_tick_callback(callback)
            finish(True)
            unreal.log_error(REPORT["exception"])
    callback = unreal.register_slate_post_tick_callback(tick)
except Exception:
    REPORT["exception"] = traceback.format_exc()
    finish(True)
    raise
