"""Package final evidence without changing engine, project source, or assets."""
import argparse, datetime, hashlib, json, shutil, zipfile
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('work',type=Path);p.add_argument('engine',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parents[3];dest=Path(__file__).resolve().parent/'Evidence'
src=a.work/'EvidenceComplete';dest.mkdir(exist_ok=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
read=lambda f:json.loads(f.read_text(encoding='utf-8-sig'))
for f in src.iterdir():
    if f.suffix in ('.png','.jpg','.json','.csv','.txt','.npz'):shutil.copy2(f,dest/f.name)
with zipfile.ZipFile(dest/'readbacks.zip','w',compression=zipfile.ZIP_STORED) as package:
    package.write(dest/'readbacks.npz','readbacks.npz')
shutil.copy2(a.work/'AutomationComplete/index.json',dest/'Automation.json')
before=read(a.work/'PreservationBefore.json');changed=[];missing=[]
for name,h in before['files'].items():
    f=repo/name
    if not f.exists():missing.append(name)
    elif sha(f)!=h:changed.append(name)
preservation={'baseline_utc':before['utc'],'checked_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'tracked_production_files':len(before['files']),'changed':changed,'missing':missing,
              'scope':'Source, Config, Content, plugin Source and plugin Content; research/docs excluded',
              'sha256':before['files']}
(dest/'Preservation.json').write_text(json.dumps(preservation,indent=2),encoding='utf-8')
engine_files=['Engine/Build/Build.version','Engine/Source/Runtime/Engine/Classes/Engine/ToonProfile.h',
 'Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionSubstrate.h',
 'Engine/Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp',
 'Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush',
 'Engine/Shaders/Private/ToonProfileCommon.ush',
 'Engine/Shaders/Private/Substrate/SubstrateDeferredLighting.ush',
 'Engine/Shaders/Private/Substrate/SubstrateEvaluation.ush',
 'Engine/Source/Runtime/StaticMeshDescription/Private/StaticMeshDescription.cpp']
sources={'engine':{name:sha(a.engine/name) for name in engine_files},
 'fixture_sha256':sha(Path(__file__).parent/'Source/ToonGPUProbe.cpp'),
 'compiled_fixture_source_sha256':sha(a.work/'Plugins/PrincipledNPR/Source/NPREditor/Private/Tests/ToonGPUProbe.cpp'),
 'research_dll_sha256':sha(a.work/'Plugins/PrincipledNPR/Binaries/Win64/UnrealEditor-NPREditor.dll'),
 'full_log_sha256':sha(a.work/'Complete.log'),
 'request_document_sha256':sha(Path('D:/downloads/PrincipledNPR_NPR_Roadmap_Toon_BSDF_Research.md'))}
(dest/'Sources.json').write_text(json.dumps(sources,indent=2),encoding='utf-8')
lines=(a.work/'Complete.log').read_text(encoding='utf-8-sig',errors='replace').splitlines()
selected=[s for s in lines if any(k in s for k in ('LogRHI:','LogD3D12RHI:','LogAutomationController:',
 'LogAutomationWorker:','LogAutomationCommandLine:','LogExit:','LogNPRRuntime:'))]
(dest/'Run.txt').write_text('Original UE log excerpt; unabridged log SHA256 in Sources.json.\n'+'\n'.join(selected)+'\n',encoding='utf-8')
shutil.copy2(a.work/'BuildFinal.txt',dest/'Build.txt')
metrics=read(dest/'measurements.json');report=read(dest/'Automation.json')
checks=dict(metrics['checks']);checks.update({'automation_success':report['succeeded']==1 and report['failed']==0,
 'production_files_preserved':not changed and not missing,
 'compiled_source_matches_delivery':sources['fixture_sha256']==sources['compiled_fixture_source_sha256']})
# NPZ members must reproduce the exact original float32 readback bytes.
import numpy as np
archive=np.load(dest/'readbacks.npz');roundtrip=all(hashlib.sha256(archive[name].tobytes()).hexdigest()==metrics['files'][name+'.rgba32f'] for name in archive.files)
checks['compressed_readbacks_lossless']=roundtrip
validation={'status':'PassedBoundedResearchChecks' if all(checks.values()) else 'Failed',
 'checked_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'captures':metrics['captures'],
 'automation_tests':1,'numeric_and_image_checks':len(metrics['checks']),'checks':checks,
 'scope':'UE 5.8.3 CL58210709, actual NVIDIA RTX4060 DX12 SM6, isolated static scene; native capabilities and constraints only',
 'negative_threshold_with_self_shadow':'Counterexample observed; not a full-domain semantic Pass',
 'unaccepted':['complete Toon adapter','Face/Hair semantic equivalence','CSM','hardware Lumen','reflections',
 'character animation','100-switch/PSO budget','cold DDC','Cook','performance','temporal production quality'],
 'artifacts_sha256':{f.name:sha(f) for f in sorted(dest.iterdir()) if f.is_file() and f.name!='Validation.json'}}
(dest/'Validation.json').write_text(json.dumps(validation,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps({k:v for k,v in validation.items() if k!='artifacts_sha256'},indent=2,ensure_ascii=False))
if not all(checks.values()):raise SystemExit(1)
