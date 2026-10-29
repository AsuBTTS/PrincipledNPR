"""Numerical checks of the supplied equations, distinct from UE/GPU validation."""
from pathlib import Path
import json,hashlib
import numpy as np

root=Path(__file__).parent
def b(q,t,w):return np.clip((q-t+w)/(2*w),0,1)
def s(z):return z*z*(3-2*z)
rng=np.random.default_rng(58210709);q=rng.uniform(-1,1,100000);t=rng.uniform(-1,1,100000);w=rng.uniform(1e-4,.5,100000)
affine=float(np.max(np.abs(b(q,t,w)-b((q+1)/2,(t+1)/2,w/2))))
def invsmooth(a):
    lo,hi=0.,1.
    for _ in range(70):
        mid=(lo+hi)/2
        if s(mid)<a:lo=mid
        else:hi=mid
    return (lo+hi)/2
hermite=2*(invsmooth(.9)-invsmooth(.1))
overlap=float(b(.02,-.1,.5)-b(.02,0,.01))
boundary=float(.1+.9*b(1,.95,.2))
out={
 'scope':'double precision numerical checks of supplied reference equations; not GPU or symbolic proof',
 'eq_4_5_affine_identity_max_abs_error':affine,
 'eq_9_2_linear_10_90_coefficient':1.6,
 'hermite_10_90_coefficient':hermite,
 'using_linear_coefficient_on_Hermite_width_ratio':hermite/1.6,
 'eq_3_12_overlap_counterexample':{'q':.02,'tau':[-.1,0],'w':[.5,.01],'colors':[0,1,0],'response':overlap},
 'eq_4_6_endpoint_restriction_example':{'tau':.95,'w':.2,'low':.1,'high':1,'true_response_at_q1':boundary,'naive_clamped_key_response':1.0},
 'eq_7_3_softness_zero':'delta=0; B_smooth(q;t,w) is only defined for w>0; explicit step-limit branch required',
 'inputs':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'Inputs').iterdir() if p.is_file()},
 'existing_results_independently_recomputed':False
}
# Independently recompute the earlier evidence, without rerendering or changing its files.
old=root.parent/'ToonGPU/Evidence';z=np.load(old/'readbacks.npz');n=z['Normals'][...,:3]
mask=(z['Mask'][...,:3].min(axis=2)>.5)&(np.linalg.norm(n,axis=2)>.9)
for _ in range(2):mask=mask&np.roll(mask,1,0)&np.roll(mask,-1,0)&np.roll(mask,1,1)&np.roll(mask,-1,1)
dot=-(n[...,0]*np.cos(np.deg2rad(55))+n[...,1]*np.sin(np.deg2rad(55)))
measure={}
for name in ('Step0','StepHigh'):
    y=z[name][...,:3].mean(axis=2);lo,hi=np.quantile(y[mask],[.05,.95]);level=(y-lo)/(hi-lo)
    region=mask&(level>.2)&(level<.8);measure[name]=float(np.median(dot[region]))
y=z['SoftPositive'][...,:3].mean(axis=2);level=(y/.80078125-.1)/.9;region=mask&(level>.2)&(level<.8)&(dot>.1)
slope,intercept=np.polyfit(level[region],dot[region],1)
measure['SoftPositive_tau']=float(intercept+slope/2);measure['SoftPositive_half_width']=float(slope/2)
out['earlier_readback_recomputation']=measure;out['existing_results_independently_recomputed']=True
target=root/'MathChecks.json';target.write_text(json.dumps(out,indent=2,ensure_ascii=False),encoding='utf-8')
assert affine<1e-11 and overlap<0 and abs(boundary-.6625)<1e-10
print(json.dumps(out,indent=2,ensure_ascii=False))
