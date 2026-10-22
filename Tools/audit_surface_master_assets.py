"""Read saved Surface graphs and recipe metadata. No asset saves or graph changes."""
import hashlib
import json
import math
from datetime import datetime, timezone
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
E=unreal.MaterialEditingLibrary
C=json.loads((ROOT/'Documentation/Surface/SurfaceContract.v1.json').read_text(encoding='utf-8'))
R=json.loads((ROOT/'Documentation/RimLight/MaterialParameters.v1.json').read_text(encoding='utf-8'))
REPORT={'status':'Running','engine':unreal.SystemLibrary.get_engine_version(),'checked_at_utc':datetime.now(timezone.utc).isoformat(),'checks':{},'saved_assets':False,'scope':'Binary parameter types/defaults and real graph connections, independent of creator text; GPU validation separate.'}

def check(name,value):
    REPORT['checks'][name]=bool(value)
    assert value,name

def pins(mat,n):
    return {str(k).replace(' ','').lower():v for k,v in zip(E.get_material_expression_input_names(n),E.get_inputs_for_material_expression(mat,n))}

try:
    for family,material_path in C['families'].items():
        mat=unreal.load_asset(material_path);check(family+'Loads',bool(mat))
        expressions=E.get_material_expressions(mat)
        calls={n.get_editor_property('material_function').get_name():n for n in expressions if isinstance(n,unreal.MaterialExpressionMaterialFunctionCall)}
        check(family+'SharedFunctions',set(C['functions']).issubset(calls))
        for p in C['parameters']:
            kind=p['type'];name=p['name']
            query={'scalar':E.get_scalar_parameter_names,'vector':E.get_vector_parameter_names,'texture':E.get_texture_parameter_names}[kind]
            check(family+name+'Type',name in set(map(str,query(mat))))
            if kind=='scalar':check(family+name+'Default',math.isclose(E.get_material_default_scalar_parameter_value(mat,name),p['default'],abs_tol=1e-6))
            if kind=='vector':
                v=E.get_material_default_vector_parameter_value(mat,name)
                check(family+name+'Default',all(math.isclose(a,b,abs_tol=1e-6) for a,b in zip([v.r,v.g,v.b],p['default'])))
            if kind=='texture':
                v=E.get_material_default_texture_parameter_value(mat,name)
                check(family+name+'Default',v and v.get_path_name().split('.')[0]==p['default'])
        compose=pins(mat,calls['MF_NPRComposeEmissive'])
        check(family+'BaseEmissivePreserved',compose['baseemissive']==calls['MF_NPRSurfaceInputs'])
        check(family+'RimAdded',compose['rimcolorcontribution']==calls['MF_RimLight'])
        check(family+'SecondModuleAdded',compose['additionalemissivecontribution']==calls['MF_NPRAdditiveColor'])
        for i,p in enumerate(R['parameters']):
            query=E.get_material_default_scalar_parameter_value if p['type']=='scalar' else E.get_material_default_vector_parameter_value
            actual=query(mat,p['material_parameter'])
            if p['type']=='scalar':check(family+p['semantic_id']+'Default',math.isclose(actual,p['default'],abs_tol=1e-6))
            else:check(family+p['semantic_id']+'Default',all(math.isclose(v,1) for v in [actual.r,actual.g,actual.b]))
            src=pins(mat,calls['MF_RimLight'])[p['function_input'].lower()]
            if i<2:
                check(family+p['semantic_id']+'DegreesToRadians',isinstance(src,unreal.MaterialExpressionMultiply) and math.isclose(src.get_editor_property('const_b'),math.pi/180,abs_tol=1e-7))
                src=E.get_inputs_for_material_expression(mat,src)[0]
            check(family+p['semantic_id']+'CorrectPin',str(src.get_editor_property('parameter_name'))==p['material_parameter'])
        if family=='DefaultLit':
            check('DefaultLitBaseColor',E.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR)==calls['MF_NPRSurfaceInputs'])
            check('DefaultLitNormal',E.get_material_property_input_node(mat,unreal.MaterialProperty.MP_NORMAL)==calls['MF_NPRSurfaceInputs'])
            check('DefaultLitEmissive',E.get_material_property_input_node(mat,unreal.MaterialProperty.MP_EMISSIVE_COLOR)==calls['MF_NPRComposeEmissive'])
        else:
            bsdf=E.get_material_property_input_node(mat,unreal.MaterialProperty.MP_FRONT_MATERIAL)
            check('RealToonBSDF',isinstance(bsdf,unreal.MaterialExpressionSubstrateToonBSDF))
            bp=pins(mat,bsdf)
            check('ToonBaseColor',bp['basecolor']==calls['MF_NPRSurfaceInputs'])
            check('ToonNormal',bp['normal']==calls['MF_NPRSurfaceInputs'])
            check('ToonEmissive',bp['emissivecolor']==calls['MF_NPRComposeEmissive'])
            check('ExistingToonProfile',bsdf.get_editor_property('toon_profile')==unreal.load_asset('/Game/NPR/Styles/AnimeFlat/Profiles/TP_S58_InitialDiffuseSeed'))
        recipe=unreal.load_asset('/PrincipledNPR/Surface/'+('SR_DefaultLit_Rim' if family=='DefaultLit' else 'SR_Toon_Rim'))
        check(family+'RecipeMaterial',recipe.get_editor_property('material')==mat)
        bindings=[b for effect in recipe.get_editor_property('effects') for b in effect.get_editor_property('parameters')]
        names=[str(b.get_editor_property('material_parameter')) for b in bindings]
        check(family+'NoDuplicateOwner',len(names)==len(set(names))==len(C['parameters'])+4)
    REPORT['asset_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'Plugins/PrincipledNPR/Content/Surface').glob('*.uasset')}
    REPORT['status']='Passed'
except Exception as error:
    REPORT.update(status='Failed',error=repr(error));raise
finally:
    p=ROOT/'Documentation/Surface/AssetAudit.json';p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
