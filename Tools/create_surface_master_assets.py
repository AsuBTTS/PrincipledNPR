"""Create owned Surface family assets from shared nodes/functions. Never edits existing Rim/Outline assets or maps.

Run in UE 5.8.3 after compiling NPRSurfaceRecipe. Existing owned assets are reused, never rebuilt over artist edits.
"""
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (ROOT / 'PrincipledNPR.uproject').is_file()
FOLDER = '/PrincipledNPR/Surface'
TAG, OWNER = 'NPRSurfaceOwner', 'PrincipledNPR.Surface.v1'
A = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
E = unreal.MaterialEditingLibrary
REPORT = {'status': 'Running', 'engine': unreal.SystemLibrary.get_engine_version(), 'assets': [],
          'render_validation': 'NotRun', 'scope': 'Owned Surface family assets; no existing actors, Rim, Outline or ToonProfile edits.'}


def owned(name, cls, factory):
    path = FOLDER + '/' + name
    if A.does_asset_exist(path):
        obj = unreal.load_asset(path)
        assert isinstance(obj, cls) and A.get_metadata_tag(obj, TAG) == OWNER, 'Refuse unowned asset: ' + path
        return obj, False
    obj = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, cls, factory())
    assert obj, path
    A.set_metadata_tag(obj, TAG, OWNER)
    return obj, True


def node(owner, cls, x=0, y=0, **props):
    create = E.create_material_expression_in_function if isinstance(owner, unreal.MaterialFunction) else E.create_material_expression
    obj = create(owner, cls, x, y)
    for k, v in props.items(): obj.set_editor_property(k, v)
    return obj


def wire(src, dst, pin, output=''):
    names=list(map(str,E.get_material_expression_input_names(dst)))
    matches=[n for n in names if n.replace(' ','').lower()==pin.replace(' ','').lower()]
    actual=matches[0] if len(matches)==1 else pin
    assert E.connect_material_expressions(src, output, dst, actual), (src.get_name(), dst.get_name(), pin, output)


def calc(owner, cls, a, b, x=0, y=0):
    n = node(owner, cls, x, y); wire(a, n, 'A'); wire(b, n, 'B'); return n


def inp(fn, name, index, vector=False, default=0):
    n = node(fn, unreal.MaterialExpressionFunctionInput, -900, index*130, input_name=name, sort_priority=index,
             input_type=unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3 if vector else unreal.FunctionInputType.FUNCTION_INPUT_SCALAR,
             use_preview_value_as_default=True)
    p = node(fn, unreal.MaterialExpressionConstant3Vector, -1150, index*130, constant=unreal.LinearColor(*default, 1)) if vector else node(fn, unreal.MaterialExpressionConstant, -1150, index*130, r=default)
    wire(p, n, 'Preview'); return n


def out(fn, name, index, src):
    n = node(fn, unreal.MaterialExpressionFunctionOutput, 1000, index*180, output_name=name, sort_priority=index)
    wire(src, n, '')


def call(mat, fn, x, y):
    n = node(mat, unreal.MaterialExpressionMaterialFunctionCall, x, y)
    assert n.set_material_function(fn); return n


def save(obj):
    assert A.save_loaded_asset(obj, False), obj.get_path_name()
    REPORT['assets'].append(obj.get_path_name())


def validation_texture(name, normal_map=False):
    path=FOLDER+'/'+name
    if A.does_asset_exist(path):
        obj=unreal.load_asset(path)
        assert A.get_metadata_tag(obj,TAG)==OWNER, 'Unowned validation texture'
        return obj
    # Authored test input, not a generated/synthetic render acceptance image.
    size=16
    data=b''.join(b'\0'+b''.join(bytes((204,128,230)) if normal_map else
        bytes((210,90,35) if ((x//4+y//4)%2) else (35,130,205)) for x in range(size)) for y in range(size))
    def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    raw=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b'')
    source=ROOT/'Saved/NPR/Surface'/f'{name}.png';source.parent.mkdir(parents=True,exist_ok=True);source.write_bytes(raw)
    task=unreal.AssetImportTask();task.filename=str(source);task.destination_path=FOLDER;task.destination_name=name;task.automated=True;task.save=False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    obj=unreal.load_asset(path);assert obj
    A.set_metadata_tag(obj,TAG,OWNER)
    if normal_map:
        obj.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
        obj.set_editor_property('srgb',False)
    save(obj);return obj


def scalar(mat, name, default, group, x, y, low=0, high=1):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=default,
                group=group, slider_min=low, slider_max=high)


def vector(mat, name, default, group, x, y):
    return node(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                default_value=unreal.LinearColor(*default, 1), group=group)


def effect(identity, output, composition, params, requires=()):
    d = unreal.NPRSurfaceEffectDefinition()
    d.set_editor_property('effect_id',identity);d.set_editor_property('output_contract',output);d.set_editor_property('composition',composition)
    d.set_editor_property('requires_effects',list(requires))
    bindings = []
    for semantic, name, kind, mapping in params:
        b = unreal.NPRSurfaceParameterBinding()
        b.set_editor_property('semantic_id',semantic);b.set_editor_property('material_parameter',name)
        b.set_editor_property('type',kind);b.set_editor_property('mapping',mapping)
        bindings.append(b)
    d.set_editor_property('parameters',bindings)
    return d


try:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/PrincipledNPR', '/Game/NPR'], True)
    rim = unreal.load_asset('/PrincipledNPR/RimLight/MF_RimLight')
    assert isinstance(rim, unreal.MaterialFunction), 'Existing MF_RimLight required'
    white = unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
    normal = unreal.load_asset('/Engine/EngineMaterials/DefaultNormal')
    assert white and normal, 'Engine white/flat-normal textures required'
    checker=validation_texture('T_SurfaceValidationChecker')
    tilt=validation_texture('T_SurfaceValidationNormal',True)

    inputs, new = owned('MF_NPRSurfaceInputs', unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew)
    if new:
        inputs.set_editor_property('description','共享表面输入：基础颜色、切线法线与原有自发光。Rim 不写入这些输出。')
        base = inp(inputs, 'BaseColorSample', 0, True, (1,1,1))
        tint = inp(inputs, 'BaseColorTint', 1, True, (.18,.18,.18))
        ntex = inp(inputs, 'NormalSampleTS', 2, True, (0,0,1))
        strength = inp(inputs, 'NormalStrength', 3, default=1)
        emit = inp(inputs, 'EmissiveSample', 4, True, (1,1,1))
        color = inp(inputs, 'EmissiveColor', 5, True, (0,0,0))
        scale = inp(inputs, 'EmissiveScale', 6, default=1)
        flat = node(inputs, unreal.MaterialExpressionConstant3Vector, -450, 360, constant=unreal.LinearColor(0,0,1,1))
        blend = node(inputs, unreal.MaterialExpressionLinearInterpolate, 0, 360)
        wire(flat, blend, 'A'); wire(ntex, blend, 'B'); wire(strength, blend, 'Alpha')
        norm = node(inputs, unreal.MaterialExpressionNormalize, 300, 360); wire(blend, norm, '')
        out(inputs, 'BaseColor', 0, calc(inputs, unreal.MaterialExpressionMultiply, base, tint, 300, 0))
        out(inputs, 'NormalTS', 1, norm)
        out(inputs, 'BaseEmissive', 2, calc(inputs, unreal.MaterialExpressionMultiply,
            calc(inputs, unreal.MaterialExpressionMultiply, emit, color, 0, 850), scale, 350, 850))
        E.update_material_function(inputs)
    save(inputs)

    additive, new = owned('MF_NPRAdditiveColor', unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew)
    if new:
        additive.set_editor_property('description','最小附加自发光模块：线性颜色乘强度。独立归属，不改写基础表面。')
        out(additive, 'AdditionalEmissiveContribution', 0, calc(additive, unreal.MaterialExpressionMultiply,
            inp(additive, 'Color', 0, True, (0,0,0)), inp(additive, 'Intensity', 1, default=0), 0, 0))
        E.update_material_function(additive)
    save(additive)

    compose, new = owned('MF_NPRComposeEmissive', unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew)
    if new:
        compose.set_editor_property('description','组合规则 v1：保留原有 Emissive，加 Rim，再加附加自发光；不限幅、不覆写 BaseColor/Normal。')
        base = inp(compose, 'BaseEmissive', 0, True, (0,0,0))
        r = inp(compose, 'RimColorContribution', 1, True, (0,0,0))
        extra = inp(compose, 'AdditionalEmissiveContribution', 2, True, (0,0,0))
        out(compose, 'Emissive', 0, calc(compose, unreal.MaterialExpressionAdd,
            calc(compose, unreal.MaterialExpressionAdd, base, r, 0, 0), extra, 400, 0))
        E.update_material_function(compose)
    save(compose)

    rim_contract = json.loads((ROOT/'Documentation/RimLight/MaterialParameters.v1.json').read_text(encoding='utf-8'))
    scalar_type, vector_type, texture_type = unreal.NPRSurfaceParameterType.SCALAR, unreal.NPRSurfaceParameterType.VECTOR, unreal.NPRSurfaceParameterType.TEXTURE
    identity, radians = unreal.NPRSurfaceParameterMapping.IDENTITY, unreal.NPRSurfaceParameterMapping.RADIANS_TO_DEGREES
    base_bindings = []
    for family, name, recipe_name in [('DefaultLit','M_NPRSurface_DefaultLit','SR_DefaultLit_Rim'),('S58Toon','M_NPRSurface_Toon','SR_Toon_Rim')]:
        profile = unreal.load_asset('/Game/NPR/Styles/AnimeFlat/Profiles/TP_S58_InitialDiffuseSeed') if family == 'S58Toon' else None
        if family == 'S58Toon': assert profile, 'Existing Toon probe profile required; do not generate/mutate it'
        mat, new = owned(name, unreal.Material, unreal.MaterialFactoryNew)
        base_bindings = []
        if new:
            mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
            mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
            E.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
            surface = call(mat, inputs, 0, 0)
            for index, (parameter, default_tex, pin, sampler) in enumerate([
                ('NPR_BaseColorTexture',white,'BaseColorSample',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                ('NPR_NormalTexture',normal,'NormalSampleTS',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                ('NPR_EmissiveTexture',white,'EmissiveSample',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)]):
                t = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1100, index*280, parameter_name=parameter,
                         texture=default_tex, sampler_type=sampler, group='基础表面')
                wire(t, surface, pin, 'RGB')
            for index, (parameter, default, pin) in enumerate([
                ('NPR_BaseColor',(.18,.18,.18),'BaseColorTint'), ('NPR_EmissiveColor',(0,0,0),'EmissiveColor')]):
                wire(vector(mat, parameter, default, '基础表面', -600, -400-index*180), surface, pin)
            for index, (parameter, default, pin) in enumerate([('NPR_NormalStrength',1,'NormalStrength'),('NPR_EmissiveScale',1,'EmissiveScale')]):
                wire(scalar(mat, parameter, default, '基础表面', -600, 850+index*160), surface, pin)
            rcall = call(mat, rim, 100, 1200)
            for index, row in enumerate(rim_contract['parameters']):
                if row['type'] == 'scalar':
                    n = scalar(mat, row['material_parameter'], row['default'], '边缘光', -1100, 1150+index*210, 0, row['ui_slider_range'][1])
                    if index < 2:
                        d = node(mat, unreal.MaterialExpressionMultiply, -550, 1150+index*210, const_b=math.pi/180)
                        wire(n,d,'A'); n=d
                else: n = vector(mat,row['material_parameter'],(1,1,1),'边缘光',-1100,1800)
                wire(n,rcall,row['function_input'])
            extra = call(mat, additive, 300, 2000)
            wire(vector(mat,'NPR_AdditionalEmissiveColor',(0,0,0),'附加自发光',-400,2000),extra,'Color')
            wire(scalar(mat,'NPR_AdditionalEmissiveScale',0,'附加自发光',-400,2180,0,4),extra,'Intensity')
            emission = call(mat,compose,850,1000)
            wire(surface,emission,'BaseEmissive','BaseEmissive'); wire(rcall,emission,'RimColorContribution','RimColorContribution')
            wire(extra,emission,'AdditionalEmissiveContribution','AdditionalEmissiveContribution')
            final = node(mat,unreal.MaterialExpressionSubstrateToonBSDF,1450,0) if profile else None
            if final: final.set_editor_property('toon_profile',profile)
            for pin, output, prop in [('BaseColor','BaseColor',unreal.MaterialProperty.MP_BASE_COLOR),('Normal','NormalTS',unreal.MaterialProperty.MP_NORMAL)]:
                if final: wire(surface,final,pin,output)
                else: assert E.connect_material_property(surface,output,prop)
            if final: wire(emission,final,'EmissiveColor','Emissive')
            else: assert E.connect_material_property(emission,'Emissive',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            for index,(parameter,default,pin,prop) in enumerate([
                ('NPR_Roughness',.5,'Roughness',unreal.MaterialProperty.MP_ROUGHNESS),
                ('NPR_Metallic',0,'Metallic',unreal.MaterialProperty.MP_METALLIC),
                ('NPR_Specular',.5,'Specular',unreal.MaterialProperty.MP_SPECULAR)]):
                n=scalar(mat,parameter,default,'基础表面',800,-350-index*160)
                if final: wire(n,final,pin)
                else: assert E.connect_material_property(n,'',prop)
            if final: assert E.connect_material_property(final,'',unreal.MaterialProperty.MP_FRONT_MATERIAL)
        errors=list(E.recompile_material(mat) or []); assert not errors, errors
        save(mat)
        for parameter,kind in [('NPR_BaseColor',vector_type),('NPR_EmissiveColor',vector_type),('NPR_BaseColorTexture',texture_type),
            ('NPR_NormalTexture',texture_type),('NPR_EmissiveTexture',texture_type),('NPR_NormalStrength',scalar_type),
            ('NPR_EmissiveScale',scalar_type),('NPR_Roughness',scalar_type),('NPR_Metallic',scalar_type),('NPR_Specular',scalar_type)]:
            base_bindings.append(('',parameter,kind,identity))
        effects=[effect('BaseSurface','BaseColor_NormalTS_BaseEmissive',unreal.NPRSurfaceComposition.SURFACE_INPUTS,base_bindings)]
        if profile: effects.append(effect('Toon','NativeToonBSDF',unreal.NPRSurfaceComposition.NATIVE_LIGHTING,[],['BaseSurface']))
        effects.append(effect('RimLight','RimColorContribution',unreal.NPRSurfaceComposition.ADDITIVE_EMISSIVE,
            [(row['semantic_id'],row['material_parameter'],scalar_type if row['type']=='scalar' else vector_type,radians if index<2 else identity)
             for index,row in enumerate(rim_contract['parameters'])],['BaseSurface']))
        effects.append(effect('AdditionalEmission','AdditionalEmissiveContribution',unreal.NPRSurfaceComposition.ADDITIVE_EMISSIVE,
            [('', 'NPR_AdditionalEmissiveColor',vector_type,identity),('', 'NPR_AdditionalEmissiveScale',scalar_type,identity)],['BaseSurface']))
        recipe,new=owned(recipe_name,unreal.NPRSurfaceRecipe,unreal.DataAssetFactory)
        if new:
            recipe.set_editor_property('recipe_id',recipe_name);recipe.set_editor_property('family_id',family)
            recipe.set_editor_property('material',mat);recipe.set_editor_property('effects',effects)
        save(recipe)
    preview,new=owned('MI_NPRSurface_Preview',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew)
    if new:
        E.set_material_instance_parent(preview,unreal.load_asset(FOLDER+'/M_NPRSurface_DefaultLit'))
        E.set_material_instance_scalar_parameter_value(preview,'边缘光强度',1)
        E.set_material_instance_vector_parameter_value(preview,'NPR_BaseColor',unreal.LinearColor(1,1,1,1))
        E.set_material_instance_texture_parameter_value(preview,'NPR_BaseColorTexture',checker)
        E.set_material_instance_texture_parameter_value(preview,'NPR_NormalTexture',tilt)
    E.update_material_instance(preview);save(preview)
    REPORT.update(status='AssetsSaved_CompileRequested',source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
except Exception as error:
    REPORT.update(status='Failed',error=repr(error));raise
finally:
    folder=ROOT/'Documentation/Surface';folder.mkdir(parents=True,exist_ok=True)
    REPORT['asset_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'Plugins/PrincipledNPR/Content/Surface').glob('*.uasset')}
    (folder/'AssetCreation.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
