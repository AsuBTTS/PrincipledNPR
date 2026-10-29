"""Package verified live-edit research evidence. Requires final GPU run and reopen to exist."""
from pathlib import Path
import json,hashlib,shutil,datetime,zipfile,re
import numpy as np

base=Path(__file__).resolve().parent;repo=base.parents[2];work=repo/'work/ToonLive';src=work/'Final3';dest=base/'Evidence';dest.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
data=read(src/'Analysis.json');updates=read(src/'updates.json')
for p in src.iterdir():
    if p.suffix in ('.json','.csv','.png','.jpg','.npz'):shutil.copy2(p,dest/p.name)
reopen=work/'ReopenFinal3';rdata={p.stem:np.fromfile(p,dtype='<f4').reshape(256,256,4) for p in reopen.glob('*.rgba32f')}
np.savez_compressed(dest/'reopen_readbacks.npz',**rdata)
shutil.copy2(reopen/'updates.json',dest/'Reopen.json')
for p in reopen.glob('Reload*.png'):shutil.copy2(p,dest/p.name)
for p in ('AutomationFinal3','AutomationReopenFinal3'):
    shutil.copy2(work/p/'index.json',dest/(p+'.json'))
shutil.copy2(work/'AutomationFinal3/index.json',dest/'Automation.json')
with zipfile.ZipFile(dest/'readbacks.zip','w',compression=zipfile.ZIP_STORED) as z:
    z.write(dest/'readbacks.npz','readbacks.npz');z.write(dest/'reopen_readbacks.npz','reopen_readbacks.npz')
with zipfile.ZipFile(dest/'research_assets.zip','w',compression=zipfile.ZIP_DEFLATED) as z:
    for p in (work/'AssetsFinal3').glob('*.uasset'):z.write(p,p.name)
before=read(work/'Before.json');changes=[p for p,h in before['files'].items() if not (repo/p).exists() or sha(repo/p)!=h]
pres={'checked_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'baseline_utc':before['utc'],
      'protected_file_count':len(before['files']),'changed_or_missing':changes,'sha256':before['files']}
(dest/'Preservation.json').write_text(json.dumps(pres,indent=2),encoding='utf-8')
inl=base/'Source/ToonLiveProbe.inl';host=repo/'work/ToonGPUResearch/Plugins/PrincipledNPR/Source/NPREditor/Private/Tests/ToonLiveProbe.inl'
engine=Path('E:/Unreal Engine/UE_5.8/Engine')
paths=['Build/Build.version','Source/Runtime/Engine/Classes/Engine/ToonProfile.h',
 'Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp','Shaders/Private/ToonProfileCommon.ush',
 'Shaders/Private/Substrate/SubstrateToonBSDF.ush','Source/Runtime/Engine/Public/ShaderCompiler.h',
 'Source/Runtime/Engine/Private/ShaderCompiler/ShaderCompilerStats.cpp','Source/Runtime/Engine/Private/Materials/MaterialInterface.cpp',
 'Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp','Source/Runtime/Renderer/Private/SceneRendering.cpp']
sources={'fixture_sha256':sha(inl),'compiled_fixture_sha256':sha(host),
         'base_fixture_sha256':sha(base.parent/'ToonGPU/Source/ToonGPUProbe.cpp'),
         'engine_files':{p:sha(engine/p) for p in paths},
         'inputs':{p.name:sha(p) for p in (base/'Inputs').iterdir()},
         'run_log_sha256':sha(work/'Final3.log'),'reopen_log_sha256':sha(work/'ReopenFinal3.log')}
(dest/'Sources.json').write_text(json.dumps(sources,indent=2),encoding='utf-8')
log=(work/'Final3.log').read_text(encoding='utf-8-sig',errors='replace').splitlines()
excerpt=[s for s in log if any(k in s for k in ('LogRHI:','LogD3D12RHI:','LogAutomationController:','LogAutomationCommandLine:','LogExit:'))]
(dest/'Run.txt').write_text('Original log excerpt; full log SHA256 in Sources.json\n'+'\n'.join(excerpt),encoding='utf-8')
failed=(work/'Final.log').read_text(encoding='utf-8-sig',errors='replace').splitlines()
attempts={'first_completed_run':{'source_sha256':sha(work/'Run1Source.inl'),
    'warmup_jobs_assigned':read(work/'Run1/updates.json')['warmup_stats']['Counters']['JobsAssigned'],
    'final_jobs_assigned':read(work/'Run1/updates.json')['final_stats']['Counters']['JobsAssigned'],
    'scope':'broader timestamp interval includes queue bubbles; original images, raw floats and JSON retained in work/ToonLive/Run1'},
 'invalid_dispatch_probe':{'source_sha256':sha(work/'DispatchAttempt.inl'),'full_log_sha256':sha(work/'Final.log'),
    'error_lines':[l for l in failed if 'may not specify' in l or 'Assertion failed:' in l],
    'verdict':'Failed before measurements; never used as a successful experiment'},
 'corrected_final_run':'Final3; no-parameter RDG pass uses None, binding CPU measured separately'}
(dest/'Attempts.json').write_text(json.dumps(attempts,indent=2),encoding='utf-8')
shutil.copy2(work/'DispatchAttempt.inl',dest/'DispatchAttempt.inl')
(dest/'FailedProbe.txt').write_text('\n'.join(attempts['invalid_dispatch_probe']['error_lines'])+'\n',encoding='utf-8')
shutil.copy2(work/'BuildFinal3.txt',dest/'Build.txt')
archive=np.load(dest/'readbacks.npz')
lossless=all(hashlib.sha256(archive[p.stem].tobytes()).hexdigest()==data['input_sha256'][p.name] for p in src.glob('*.rgba32f'))
checks=dict(data['checks']);checks.update({'protected_files_unchanged':not changes,'fixture_matches_compiled_source':sha(inl)==sha(host),
    'raw_readback_archive_lossless':lossless,'assets_saved':all(read(src/'persistence.json')[k] for k in ('profile_saved','material_saved')),
    'main_automation_passed':read(work/'AutomationFinal3/index.json')['failed']==0 and read(work/'AutomationFinal3/index.json')['succeeded']==1,
    'fresh_process_automation_passed':read(work/'AutomationReopenFinal3/index.json')['failed']==0 and read(work/'AutomationReopenFinal3/index.json')['succeeded']==1})
validation={'status':'PassedBoundedResearchChecks' if all(checks.values()) else 'Failed','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'dynamic_updates':data['steps'],'main_captures':data['captures'],'reopen_captures':len(rdata),'checks':checks,
 'scope':'Editor preview, UE5.8.3 CL58210709, RTX4060 DX12 SM6; Runtime API experiment also executed only in Editor',
 'runtime_cook':'NotRun','actual_sources':sources,
 'artifact_sha256':{p.name:sha(p) for p in dest.iterdir() if p.is_file() and p.name!='Validation.json'}}
(dest/'Validation.json').write_text(json.dumps(validation,indent=2),encoding='utf-8')
names={'A':'A 私有 Profile','D':'D 预制材质','B':'B MID 偏移','C':'C 偏移纹理+通知','Natural':'A 自然渲染','RuntimeAPI':'底层 C++ 队列（Editor）'}
table='| 路线 | 同条件阈值样本 | 增量 JobsAssigned | 最大中心误差（dot） | 最大 HDR RMSE |\n| --- | --- | --- | --- | --- |\n'
for route in names:
    g=data['groups'][route];table+=f"| {names[route]} | 12 | {g['jobs_assigned_delta']} | {g['threshold_sweep_max_abs_tau_error']:.8f} | {g['threshold_sweep_max_HDR_RMSE']:.8f} |\n"
table+=f"\n最终进程预热后/测量后累计 JobsAssigned 均为 {updates['warmup_stats']['Counters']['JobsAssigned']}；累计材质翻译次数均为 {updates['warmup_stats']['MaterialCounters']['MaterialTranslateCalls']}。较早首轮预热产生 6 个编译任务，更新区间仍未增加，详见 Attempts.json。\n"
table+=f"\n新进程重开与保存前 HDR RMSE 为 {data['reopen_HDR_RMSE']:.8f}。\n"
perf='同一 12 点阈值扫描，单位 ms；每格为中位数 / P95。\n\n| 路线 | 绑定入队 CPU | 编辑/生成 CPU | Atlas 准备/提交 CPU | GPU dispatch 区间 |\n| --- | --- | --- | --- | --- |\n'
for route in ('A','D','B','C'):
    c=data['groups'][route]['same_condition_sweep_cost'];values=[]
    for key in ('bind_enqueue_cpu_ms','edit_cpu_ms','atlas_build_cpu_ms','atlas_dispatch_interval_gpu_ms'):
        v=c[key];values.append(f"{v['median']:.4f} / {v['p95']:.4f}" if v else '未测')
    perf+=f"| {names[route]} | "+' | '.join(values)+' |\n'
perf+='\nB/D 的 Atlas/GPU 项是已缓存情况下的 probe 底噪，不代表存在同等更新工作。完整较宽计时区间、带 Flush 的绑定时间及逐次数据保存在 updates.json。\n'
report=base/'Report.md';text=report.read_text(encoding='utf-8')
text=re.sub(r'<!-- LIVE_METRICS_START -->.*?<!-- LIVE_METRICS_END -->','<!-- LIVE_METRICS_START -->\n'+table+'<!-- LIVE_METRICS_END -->',text,flags=re.S)
text=re.sub(r'<!-- LIVE_PERF_START -->.*?<!-- LIVE_PERF_END -->','<!-- LIVE_PERF_START -->\n'+perf+'<!-- LIVE_PERF_END -->',text,flags=re.S)
report.write_text(text,encoding='utf-8')
print(json.dumps({k:v for k,v in validation.items() if k not in ('actual_sources','artifact_sha256')},indent=2))
if not all(checks.values()):raise SystemExit(1)
