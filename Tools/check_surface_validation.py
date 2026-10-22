"""Collect independent real-asset, automation, HDR and preservation gates. Run with the bundled Python (Pillow)."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Documentation/Surface'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
audit=read(OUT/'AssetAudit.json')
automation=read(ROOT/'Saved/Automation/SurfaceFinal/index.json')
gpu=read(OUT/'Validation/GpuMetrics.json')
rim=read(OUT/'RimRegression/ImageMetrics.json')
checks={}
tests=[t for t in automation['tests'] if t['fullTestPath'].startswith('NPR.Surface.')]
checks['TwoSurfaceAutomationTests']=len(tests)==2 and all(t['state']=='Success' and t['errors']==0 for t in tests)
checks['NinetyBinaryAssetChecks']=audit['status']=='Passed' and len(audit['checks'])==90 and all(audit['checks'].values())
checks['AuditedSavedAssetHashes']=all(hashlib.sha256((ROOT/'Plugins/PrincipledNPR/Content/Surface'/name).read_bytes()).hexdigest()==h for name,h in audit['asset_sha256'].items())
checks['BaseColorPreserved']=gpu['base_color_rim_difference']<1e-5
checks['NormalPreserved']=gpu['normal_rim_difference']<1e-5
checks['NormalTextureActuallyUsed']=gpu['normal_texture_difference']>.01
checks['BaseEmissiveVisible']=gpu['base_emissive_difference']>.01
checks['RimVisible']=gpu['rim_difference']>.01
checks['SecondEffectVisible']=gpu['second_effect_difference']>.01
checks['HDRContributionsAdd']=gpu['hdr_relative_addition_error']<.01
checks['ScreenOutlineVisible']=gpu['screen_outline_difference']>.001
checks['HullOutlineVisible']=gpu['hull_outline_difference']>.001
checks['ToonAndRimVisible']=gpu['toon_rim_difference']>.01
pngs=sorted((OUT/'Validation').glob('*.png'))
checks['SeventeenReal512Captures']=len(pngs)==17 and all(Image.open(p).size==(512,512) for p in pngs)
checks['IndependentRimRegression']=rim['status']=='Passed' and all(rim['checks'].values())
baseline=ROOT/'Saved/NPR/Surface/Before.json'
changed=[]
if baseline.exists():
    for name,h in read(baseline)['sha256'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=h:changed.append(name)
    checks['ExistingRimOutlineSourcesAssetsMapsEvidenceUnchanged']=not changed
else:
    checks['ExistingRimOutlineSourcesAssetsMapsEvidenceUnchanged']=False
for key,log in [('GameBuild','GameBuild.log'),('MainEditorBuild','MainEditorFinalBuild.log')]:
    checks[key]='Result: Succeeded' in (ROOT/'Saved/NPR/Surface'/log).read_text(encoding='utf-8-sig')
report={
    'status':'Passed' if all(checks.values()) else 'Failed', 'checked_at_utc':datetime.now(timezone.utc).isoformat(),
    'scope':'UE 5.8.3 / CL 58210709; main-project Surface Composer and isolated 512px sphere GPU captures; BaseColor/Normal/HDR addition, AdditionalEmission, native Toon+Rim, Screen/Hull coexistence. Independent Rim regression executed in validation project to preserve old evidence.',
    'engine':audit['engine'],
    'devices':[{k:d.get(k) for k in ['platform','oSVersion','gPU','renderMode','rHI']} for d in automation.get('devices',[])],
    'checks':checks,'gpu_metrics':gpu,
    'surface_tests':[{k:t.get(k) for k in ['fullTestPath','state','errors','warnings','duration']} for t in tests],
    'rim_regression':{'checks':rim['checks'],'automation':rim['automation']},
    'unchanged_existing_files':not changed,'unexpected_changes':changed,
    'captures_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in pngs},
    'asset_sha256':audit['asset_sha256'],
    'source_sha256':{str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in [
        ROOT/'Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRSurfaceRecipe.h',ROOT/'Plugins/PrincipledNPR/Source/NPRRuntime/Private/NPRSurfaceRecipe.cpp',
        ROOT/'Plugins/PrincipledNPR/Source/NPRRuntime/Private/Tests/NPRSurfaceComposerTests.cpp',ROOT/'Plugins/PrincipledNPR/Source/NPREditor/Private/Tests/NPRSurfaceRenderTests.cpp']},
    'validation_corrections':'Initial captures included fallback material. Final test explicitly completes assigned texture/shader work and retains capture view state; failed initial attempts preserved separately.',
    'limits':['No complete Style Profile resolver, editor modulation page or automatic arbitrary source-graph conversion.',
              'No character animation, transparency, arbitrary LOD, shadow/GI quality, Cook or performance acceptance.',
              'Toon family uses the existing uncalibrated Game probe profile; does not certify full S58 semantic mapping.',
              'New technical Surface inputs use native MID setters; those setters do not enforce contract numeric domains.',
              'Existing open Editor process must be reopened to load new reflected classes; UBT generated suffixed main-project DLLs without replacing loaded old binaries.']
}
(OUT/'ImplementationValidation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(report['status'],json.dumps(checks,ensure_ascii=False))
assert all(checks.values()),'Surface gate failed; inspect ImplementationValidation.json'
