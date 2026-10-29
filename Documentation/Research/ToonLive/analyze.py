"""Analyze live Profile experiments from raw GPU float32 readbacks or a lossless NPZ archive."""
import argparse,json,hashlib,csv
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw

p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--reopen',type=Path);a=p.parse_args();d=a.directory
loadjson=lambda f:json.loads(f.read_text(encoding='utf-8-sig'))
run=loadjson(d/'updates.json');steps=run['steps']
archive=np.load(d/'readbacks.npz') if (d/'readbacks.npz').exists() else None
rgba={}
def get(n):
    if n not in rgba:
        f=d/(n+'.rgba32f');rgba[n]=np.fromfile(f,dtype='<f4').reshape(256,256,4) if f.exists() else archive[n]
    return rgba[n][...,:3]
mask=(get('Mask').min(axis=2)>.5)&(np.linalg.norm(get('Normals'),axis=2)>.9)
for _ in range(2):mask&=np.roll(mask,1,0)&np.roll(mask,-1,0)&np.roll(mask,1,1)&np.roll(mask,-1,1)
n=get('Normals');q=-(n[...,0]*np.cos(np.deg2rad(55))+n[...,1]*np.sin(np.deg2rad(55)))
scale=float(np.median(get('Mask')[mask]));measure=[]
def delta(row,section,key):return row['stats_after'][section][key]-row['stats_before'][section][key]
for row in steps:
    metrics={k:row[k] for k in ('name','route','tau','width','shared','two_slots','self_shadow','include_shadow','natural_renderer_update')}
    for f in (0,1):
        im=get(row['name']+f'_f{f}');y=im.mean(axis=2);w=row['width'];tau=row['tau']
        z=np.clip((q-tau+w)/(2*w),0,1) if w else np.where(q<tau,0,np.where(q>tau,1,.5))
        if row['smooth']:z=z*z*(3-2*z)
        reference=scale*(.1+.9*z)
        metrics[f'f{f}_rmse_linear_hdr']=float(np.sqrt(np.mean((y[mask]-reference[mask])**2)))
        region=mask&(((y/scale-.1)/.9)>.2)&(((y/scale-.1)/.9)<.8)
        if region.sum()>12 and not row['two_slots']:
            level=(y/scale-.1)/.9
            slope,intercept=np.polyfit(level[region],q[region],1)
            metrics[f'f{f}_fit_tau']=float(intercept+slope/2);metrics[f'f{f}_fit_halfwidth']=float(slope/2)
            shape_level=level[region]
            if row['smooth']:
                lo=np.zeros_like(shape_level);hi=np.ones_like(shape_level)
                for _ in range(35):
                    mid=(lo+hi)/2;sel=mid*mid*(3-2*mid)<shape_level;lo=np.where(sel,mid,lo);hi=np.where(sel,hi,mid)
                shape_level=(lo+hi)/2
            shape_slope,shape_intercept=np.polyfit(shape_level,q[region],1)
            metrics[f'f{f}_reference_shape_tau']=float(shape_intercept+shape_slope/2)
            metrics[f'f{f}_reference_shape_halfwidth']=float(shape_slope/2)
        else:
            metrics[f'f{f}_fit_tau']=metrics[f'f{f}_fit_halfwidth']=None
            metrics[f'f{f}_reference_shape_tau']=metrics[f'f{f}_reference_shape_halfwidth']=None
    metrics.update({'jobs_assigned_delta':delta(row,'Counters','JobsAssigned'),
                    'jobs_completed_delta':delta(row,'Counters','JobsCompleted'),
                    'unique_shader_inputs_delta':delta(row,'Counters','UniqueCacheInputHashes'),
                    'material_translate_delta':delta(row,'MaterialCounters','MaterialTranslateCalls'),
                    'material_cache_hits_delta':delta(row,'MaterialCounters','MaterialCacheHits'),
                    'same_resource':row['same_material_resource'],'same_shader_map':row['same_shader_map'],
                    'atlas_rebuilt_observed':bool(row['atlas_invalid_before_probe'] and row['atlas_valid_after_probe'])})
    measure.append(metrics)
groups={}
for route in sorted({r['route'] for r in steps}):
    rows=[r for r in steps if r['route']==route];m=[r for r in measure if r['route']==route]
    group={'n':len(rows),'jobs_assigned_delta':sum(r['jobs_assigned_delta'] for r in m),
        'material_translations_delta':sum(r['material_translate_delta'] for r in m),
        'atlas_rebuilds_observed':sum(r['atlas_rebuilt_observed'] for r in m),
        'notify_calls':sum(r['notify_calls'] for r in rows),'runtime_update_calls':sum(r['runtime_update_calls'] for r in rows),
        'texture_upload_calls':sum(r['texture_upload_calls'] for r in rows)}
    for key in ('bind_enqueue_cpu_ms','bind_and_flush_cpu_ms','edit_cpu_ms','atlas_build_cpu_ms','atlas_gpu_ms','atlas_dispatch_interval_gpu_ms','capture_and_readback_cpu_ms'):
        values=[r[key] for r in rows if key in r and r[key]>=0 and (not key.startswith('atlas_') or r['atlas_probe_calls']>0)]
        group[key]={'median':float(np.median(values)),'p95':float(np.quantile(values,.95)),'max':float(max(values))} if values else None
    # Compare the same threshold sweep across routes; exclude intentionally stale and shadow cases.
    sweep=[r for r in m if '_T' in r['name']]
    group['threshold_sweep_max_abs_tau_error']=max((abs(r['f0_fit_tau']-r['tau']) for r in sweep if r['f0_fit_tau'] is not None),default=None)
    group['threshold_sweep_max_HDR_RMSE']=max((r['f0_rmse_linear_hdr'] for r in sweep),default=None)
    sweep_rows=[r for r in rows if '_T' in r['name']]
    group['same_condition_sweep_cost']={}
    for key in ('bind_enqueue_cpu_ms','bind_and_flush_cpu_ms','edit_cpu_ms','atlas_build_cpu_ms','atlas_dispatch_interval_gpu_ms','capture_and_readback_cpu_ms'):
        vals=[r[key] for r in sweep_rows if key in r and r[key]>=0 and (not key.startswith('atlas_') or r['atlas_probe_calls']>0)]
        group['same_condition_sweep_cost'][key]={'median':float(np.median(vals)),'p95':float(np.quantile(vals,.95))} if vals else None
    groups[route]=group
checks={'all_readbacks_finite':all(np.isfinite(x).all() for x in rgba.values()),
        'no_incremental_assigned_jobs':all(r['jobs_assigned_delta']==0 for r in measure),
        'no_incremental_material_translations':all(r['material_translate_delta']==0 for r in measure),
        'all_selected_resources_and_shader_maps_stable':all(r['same_resource'] and r['same_shader_map'] for r in measure)}
for route in ('A','B','C','D','Natural','RuntimeAPI'):
    checks[route+'_threshold_error_under_dot_0_02']=groups[route]['threshold_sweep_max_abs_tau_error'] is not None and groups[route]['threshold_sweep_max_abs_tau_error']<.02
yy,xx=np.indices(mask.shape)
isolation={}
for prefix in ('Slots','Shared'):
    before=get(prefix+'Before_f0');after=get(prefix+'After_f0');region=before.max(axis=2)>1e-5
    isolation[prefix]={side:float(np.abs(after-before)[region&select].mean()) for side,select in [('left',xx<128),('right',xx>=128)]}
checks['distinct_profile_isolation']=isolation['Slots']['left']>.05 and isolation['Slots']['right']<.005
checks['shared_profile_fans_out']=isolation['Shared']['left']>.05 and isolation['Shared']['right']>.05
byname={r['name']:r for r in measure}
stale=byname['C_Stale'];refreshed=byname['C_Refresh']
checks['same_size_texture_without_profile_notify_is_stale']=abs(stale['f0_fit_tau']-stale['tau'])>.1
checks['profile_notify_makes_texture_upload_visible']=abs(refreshed['f0_fit_tau']-refreshed['tau'])<.02
if a.reopen:
    rdir=a.reopen
    reopened=np.fromfile(rdir/'Reload_f1.rgba32f',dtype='<f4').reshape(256,256,4)
    written=get('PersistWritten');rmse=float(np.sqrt(np.mean((written[mask]-reopened[...,:3][mask])**2)))
    checks['fresh_process_saved_assets_match_HDR']=rmse<.005
else:rmse=None
out={'scope':'new dynamic GPU experiments; old readbacks analyzed separately in MathChecks.json',
     'steps':len(steps),'captures':len(rgba),'actual_gpu':run['actual_rhi_adapter'],'scale_from_basecolor_buffer':scale,
     'groups':groups,'per_update':measure,'isolation':isolation,'reopen_HDR_RMSE':rmse,'checks':checks,
     'timing_limit':'timestamp intervals include pass barriers/queue bubbles; total probe is not pure compute. Dispatch interval excludes CPU graph construction and may exclude queued curve uploads. No production frame-rate claim.',
     'atlas_dimensions':{k:steps[0][k] for k in ('atlas_x','atlas_y','atlas_slices')},
     'resource_update_counts':'observed invalid->valid around exported native atlas updater; natural-renderer cases have no instrumented rebuild count',
     'input_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in d.iterdir() if p.suffix=='.rgba32f' or p.name in ('updates.json','cold_stats_before.json','cold_stats_after.json','persistence.json')}}
(d/'Analysis.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
with (d/'summary.csv').open('w',newline='',encoding='utf-8') as f:
    writer=csv.DictWriter(f,fieldnames=list(measure[0]));writer.writeheader();writer.writerows(measure)
if archive is None:
    np.savez_compressed(d/'readbacks.npz',**rgba)
for name,names in {
 '01_live_threshold':['A_T00','A_T05','A_T11','D_T00','D_T05','D_T11','B_T00','B_T05','B_T11','C_T00','C_T05','C_T11'],
 '02_lifecycle':['SlotsBefore','SlotsAfter','SharedBefore','SharedAfter','C_Stale','C_Refresh','NegativeSelf','NegativeClear','Hermite','ZeroWidth','Boundary','Coalesced10']
}.items():
    sheet=Image.new('RGB',(3*272,((len(names)+2)//3)*292),(240,242,243));draw=ImageDraw.Draw(sheet)
    for i,n in enumerate(names):
        f=d/(n+'_f0.png');x=i%3*272+8;y=i//3*292+28
        sheet.paste(Image.open(f).convert('RGB'),(x,y));draw.text((x,y-21),n,fill=(20,40,35))
    sheet.save(d/(name+'.jpg'),quality=92)
try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(1,2,figsize=(10,3.7),layout='constrained')
    for route in ('A','D','B','C'):
        values=[r for r in measure if r['route']==route and '_T' in r['name']]
        target=np.array([r['tau'] for r in values]);actual=np.array([r['f0_fit_tau'] for r in values])
        axes[0].plot(target,actual,'o-',label=route,markersize=3)
        axes[1].plot(target,actual-target,'o-',label=route,markersize=3)
    axes[0].plot([.15,.7],[.15,.7],'k:',label='reference');axes[0].set(xlabel='Target signed-dot center',ylabel='GPU fitted center')
    axes[1].axhline(0,color='k',linestyle=':');axes[1].set(xlabel='Target signed-dot center',ylabel='Center error (signed dot)')
    for ax in axes:ax.grid(alpha=.2);ax.legend()
    fig.savefig(d/'03_threshold_error.png',dpi=170);plt.close(fig)
except ImportError:
    pass
print(json.dumps({k:v for k,v in out.items() if k not in ('per_update','input_sha256')},indent=2))
