"""Editor-only reproducible Outline shaders. This is not required for normal tool use.
Run with UE 5.8 -ExecutePythonScript; creates only owned /PrincipledNPR/Outline assets.
"""
import json
import os
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / "PrincipledNPR.uproject").is_file(), "Wrong project"
A = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
M = unreal.MaterialEditingLibrary
OWNER = "PrincipledNPR.OutlineSlice.v1"
REPORT = {"owner": OWNER, "assets": [], "status": "Running"}


def node(mat, cls, **properties):
    result = M.create_material_expression(mat, cls)
    for key, value in properties.items():
        result.set_editor_property(key, value)
    return result


def connect(src, dst, name):
    assert M.connect_material_expressions(src, "", dst, name), name


def output(src, prop):
    assert M.connect_material_property(src, "", prop), str(prop)


def scalar(mat, name, value):
    return node(mat, unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)


def material(name, post=False):
    path = "/PrincipledNPR/Outline/" + name
    if A.does_asset_exist(path):
        mat = A.load_asset(path)
        assert A.get_metadata_tag(mat, "NPROutlineOwner") == OWNER, "Refuse unowned asset: " + path
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, "/PrincipledNPR/Outline", unreal.Material, unreal.MaterialFactoryNew())
    assert mat
    M.delete_all_material_expressions(mat)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS if post else unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE if post else unreal.BlendMode.BLEND_MASKED)
    if not post:
        M.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    A.set_metadata_tag(mat, "NPROutlineOwner", OWNER)
    REPORT["assets"].append(path)
    return mat


def save(mat):
    M.layout_material_expressions(mat)
    errors = M.recompile_material(mat)
    assert not errors, str(errors)
    if os.environ.get("NPR_OUTLINE_BACKEND_NO_SAVE") != "1":
        assert A.save_loaded_asset(mat, False), mat.get_path_name()


def width_mask_default():
    """Owned, linear Masks sampler default; never changes an Engine texture."""
    path = "/PrincipledNPR/Outline/T_OutlineWidthMaskWhite"
    if A.does_asset_exist(path):
        texture = A.load_asset(path)
        assert A.get_metadata_tag(texture, "NPROutlineOwner") == OWNER, "Refuse unowned texture: " + path
    else:
        texture = A.duplicate_asset("/Engine/EngineResources/WhiteSquareTexture", path)
    assert texture
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    texture.set_editor_property("virtual_texture_streaming", False)
    texture.set_editor_property("never_stream", True)
    A.set_metadata_tag(texture, "NPROutlineOwner", OWNER)
    if os.environ.get("NPR_OUTLINE_BACKEND_NO_SAVE") != "1":
        assert A.save_loaded_asset(texture, False)
    REPORT["assets"].append(path)
    return texture


try:
    white_width_mask = width_mask_default()
    mask = material("M_OutlineMask")
    output(scalar(mask, "Visible", 1), unreal.MaterialProperty.MP_OPACITY_MASK)
    save(mask)

    hull = material("M_OutlineHull")
    hull.set_editor_property("two_sided", True)
    color = node(hull, unreal.MaterialExpressionVectorParameter, parameter_name="OutlineColor",
                 default_value=unreal.LinearColor(0.015, 0.008, 0.025, 1))
    output(color, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    sign = node(hull, unreal.MaterialExpressionTwoSidedSign)
    negative = node(hull, unreal.MaterialExpressionMultiply, const_b=-1.0)
    connect(sign, negative, "A")
    output(negative, unreal.MaterialProperty.MP_OPACITY_MASK)
    normal = node(hull, unreal.MaterialExpressionVertexNormalWS)
    width = scalar(hull, "WidthCm", 0.1)
    vertex = node(hull, unreal.MaterialExpressionVertexColor)
    red = node(hull, unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    assert M.connect_material_expressions(vertex, "", red, "")
    local = node(hull, unreal.MaterialExpressionLinearInterpolate, const_a=1.0)
    connect(red, local, "B")
    connect(scalar(hull, "UseVertexColorWidth", 0), local, "Alpha")
    local_width = node(hull, unreal.MaterialExpressionMultiply)
    connect(width, local_width, "A")
    connect(local, local_width, "B")
    # Explicit mip level is required for WPO's vertex-stage texture fetch.
    width_mask = node(hull, unreal.MaterialExpressionTextureSampleParameter2D,
                      parameter_name="WidthMask", texture=white_width_mask,
                      sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                      mip_value_mode=unreal.TextureMipValueMode.TMVM_MIP_LEVEL, const_mip_value=0,
                      const_coordinate=0, automatic_view_mip_bias=False)
    mask_red = node(hull, unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    connect(width_mask, mask_red, "")
    mask_saturate = node(hull, unreal.MaterialExpressionSaturate)
    connect(mask_red, mask_saturate, "")
    mask_factor = node(hull, unreal.MaterialExpressionLinearInterpolate, const_a=1.0)
    connect(mask_saturate, mask_factor, "B")
    connect(scalar(hull, "UseWidthMask", 0), mask_factor, "Alpha")
    masked_width = node(hull, unreal.MaterialExpressionMultiply)
    connect(local_width, masked_width, "A")
    connect(mask_factor, masked_width, "B")
    offset = node(hull, unreal.MaterialExpressionMultiply)
    connect(normal, offset, "A")
    connect(masked_width, offset, "B")
    output(offset, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    save(hull)

    screen = material("M_OutlineScreen", post=True)
    # UE 5.8: After DOF is BEFORE the temporal upscaler. Before Bloom is AFTER it.
    # CustomDepth and SceneDepth share temporal jitter here; TSR/TAA resolves the outline.
    screen.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_DOF)
    screen.set_editor_property("blendable_priority", 100)
    # Distinct stencil groups require independent passes, not averaged MID parameters.
    screen.set_editor_property("is_blendable", False)
    names = ["SceneColor", "CurrentStencil", "CurrentDepth", "OutlineColor", "WidthPixels", "StencilId", "DepthBiasCm"]
    inputs = []
    for name in names:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    custom = node(screen, unreal.MaterialExpressionCustom, inputs=inputs,
                  output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description="Temporal-resolved visible silhouette dilation (before TSR/TAA)")
    custom.set_editor_property("code", r"""
// Width is expressed in final display pixels, even when TSR renders at a lower resolution.
float width = clamp(WidthPixels, 0.0, 8.0) * View.ViewResolutionFraction;
float inside = abs(CurrentStencil.r - StencilId) < 0.5;
if (width < 0.01 || inside > 0.5) return SceneColor.rgb;
float2 uv = GetDefaultSceneTextureUV(Parameters, 25);
float4 bounds = GetSceneTextureUVMinMax(25);
// Use the texture extent, not the viewport extent (editor views can share larger buffers).
float2 pixelStep = GetSceneTextureBufferSize(25).zw;
float nearest = 1e10;
float found = 0.0;
[unroll] for (int ring = 1; ring <= 4; ++ring)
{
    [unroll] for (int dir = 0; dir < 16; ++dir)
    {
        float angle = dir * 0.392699081698724;
        float2 p = clamp(uv + float2(cos(angle), sin(angle)) * width * (ring * 0.25) * pixelStep, bounds.xy, bounds.zw);
        float stencil = SceneTextureLookup(Parameters, p, 25, false).r;
        if (abs(stencil - StencilId) < 0.5)
        {
            found = 1.0;
            nearest = min(nearest, SceneTextureLookup(Parameters, p, 13, false).r);
        }
    }
}
float visible = nearest <= CurrentDepth.r + DepthBiasCm;
return lerp(SceneColor.rgb, OutlineColor.rgb, found * visible);
""")
    for name, texture_id in [("SceneColor", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
                             ("CurrentStencil", unreal.SceneTextureId.PPI_CUSTOM_STENCIL),
                             ("CurrentDepth", unreal.SceneTextureId.PPI_SCENE_DEPTH)]:
        connect(node(screen, unreal.MaterialExpressionSceneTexture, scene_texture_id=texture_id), custom, name)
    connect(node(screen, unreal.MaterialExpressionVectorParameter, parameter_name="OutlineColor",
                 default_value=unreal.LinearColor(0.015, 0.008, 0.025, 1)), custom, "OutlineColor")
    for name, value in [("WidthPixels", 1.25), ("StencilId", 1), ("DepthBiasCm", 0.1)]:
        connect(scalar(screen, name, value), custom, name)
    output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    save(screen)
    REPORT["status"] = "CreatedAndSaved_ShaderCompletionPending"
finally:
    target = ROOT / "Saved/NPR/Outline/BackendCreation.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
