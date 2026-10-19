"""Open the owned textured character in the complete actor comparison view.
Normal editor startup: -ExecCmds="py <absolute script path>" (no validation session).
"""
from pathlib import Path
import re
import unreal

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lib = unreal.NPROutlineEditorLibrary
map_path = "/Game/NPR/Maps/L_Outline_MultiScope_v1"
assert levels.load_level(map_path)
found = [a for a in actors.get_all_level_actors() if a.get_actor_label() == "Outline_TexturedCharacter"]
if found:
    demo = found[0]
    group = demo.get_component_by_class(unreal.NPROutlineActorComponent)
else:
    demo = actors.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(1500, 0, 0))
    demo.set_actor_label("Outline_TexturedCharacter")
    demo.tags = [unreal.Name("PrincipledNPR.MultiScope.v1")]
    mesh = demo.get_component_by_class(unreal.SkeletalMeshComponent)
    mesh.set_skeletal_mesh_asset(assets.load_asset("/Game/EndfieldRender/Character/Qianyu/SKM_Chen"))
    for i in range(mesh.get_num_materials()):
        path = "/Game/NPR/Characters/ChenQianyu/Materials/SemiPBRStylized/MI_ChenQianyu_%02d_Initial" % i
        material = assets.load_asset(path) if assets.does_asset_exist(path) else assets.load_asset(
            "/Game/NPR/Characters/ChenQianyu/Materials/M_ExcludedSourceProxy")
        if material:
            mesh.set_material(i, material)
    result = lib.configure_actor(demo)
    group = result[0] if isinstance(result, tuple) else result
    assert group
    values = unreal.NPROutlineSettings()
    values.method = unreal.NPROutlineMethod.SCREEN_SPACE
    values.color = unreal.LinearColor(0.01, 0.01, 0.015, 1)
    values.width_pixels = 1.25
    group.scope = unreal.NPROutlineScope.UNIFIED_SILHOUETTE
    group.base_settings = values
    rules = list(group.meshes)
    for attempt in range(mesh.get_num_materials() + 1):
        if group.refresh_outline():
            break
        # Preserve unsupported source surfaces, explicitly exclude only their outline coverage.
        error = group.get_last_error()
        slot = re.search(r"slot (\d+) needs Opaque coverage", error)
        assert slot, error
        material_rule = unreal.NPROutlineMaterialRule()
        material_rule.material_slot_name = mesh.get_material_slot_names()[int(slot.group(1))]
        patch = unreal.NPROutlineOverride(override_enabled=True, enabled=False)
        material_rule.override = patch
        rules[0].materials = list(rules[0].materials) + [material_rule]
        group.meshes = rules
    assert group.refresh_outline(), group.get_last_error()
    preset_result = lib.save_preset("/Game/NPR/Outline/MultiScope/OP_TexturedCharacter", values)
    preset = preset_result[0] if isinstance(preset_result, tuple) else preset_result
    assert preset
    group.apply_preset(preset)
    assert lib.save_actor_outline(group) is not None
assert group.refresh_outline(), group.get_last_error()
actors.set_selected_level_actors([demo])
lib.open_editor()
lib.set_comparison_capture_size(0, 0)
