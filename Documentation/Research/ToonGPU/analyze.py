"""Measure real UE readbacks. PNG contact sheets are presentation only, never evidence substitutes."""
import argparse, csv, hashlib, json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args();root=a.directory
rows=list(csv.DictReader((root/'cases.csv').open(encoding='utf-8-sig')))
archive=np.load(root/'readbacks.npz') if (root/'readbacks.npz').exists() else None
rgba={r['name']:(np.fromfile(root/(r['name']+'.rgba32f'),dtype='<f4').reshape(256,256,4)
      if (root/(r['name']+'.rgba32f')).exists() else archive[r['name']]) for r in rows}
data={n:x[...,:3] for n,x in rgba.items()}
norm=data['Normals'];mask=(data['Mask'].min(axis=2)>.5)&(np.linalg.norm(norm,axis=2)>.9)
# Erode two pixels to exclude silhouette AA and framebuffer coverage.
for _ in range(2): mask=mask&np.roll(mask,1,0)&np.roll(mask,-1,0)&np.roll(mask,1,1)&np.roll(mask,-1,1)
def lum(name):return data[name].mean(axis=2)
def diff(x,y,region=mask):return float(np.abs(data[x]-data[y])[region].mean())
def energy(name,region=mask):return float(lum(name)[region].mean())
results={'source':'UE GPU RGBA16F render target read back as RGBA32F; measured RGB, no display transform',
         'sphere_pixels':int(mask.sum()),'captures':len(rows),'energy':{r['name']:energy(r['name']) for r in rows},'thresholds':{}}
for name,tau in [('StepLow',-.5),('Step0',0),('StepHigh',.5)]:
    yaw=np.deg2rad(55);ndotl=-(norm[...,0]*np.cos(yaw)+norm[...,1]*np.sin(yaw))
    y=lum(name);lo,hi=np.quantile(y[mask],[.05,.95]);z=(y-lo)/max(hi-lo,1e-8)
    band=mask&(z>.2)&(z<.8)
    # LUT interpolation may leave no pixels in this band; report null instead of fabricating a fit.
    measured=float(np.median(ndotl[band])) if band.any() else None
    stable=mask&(np.abs(ndotl-tau)>.04)
    wrong=float(np.mean((z[stable]>.5)!=(ndotl[stable]>tau)))
    results['thresholds'][name]={'tau':tau,'ramp_u':(tau+1)/2,'fitted_tau':measured,
        'transition_pixels':int(band.sum()),'classification_error_excluding_dot_margin_0_04':wrong,
        'plateau_low':float(lo),'plateau_high':float(hi)}
if 'SoftPositive' in data:
    yaw=np.deg2rad(55);n=-(norm[...,0]*np.cos(yaw)+norm[...,1]*np.sin(yaw))
    normalized=(lum('SoftPositive')/.80078125-.1)/.9
    region=mask&(normalized>.2)&(normalized<.8)&(n>.1)
    slope,intercept=np.polyfit(normalized[region],n[region],1)
    results['softness_positive']={'target_tau':.5,'target_dot_half_width':.15,
        'fitted_tau':float(intercept+slope/2),'fitted_dot_half_width':float(slope/2),'fit_pixels':int(region.sum())}
pairs=[('Dark','Step0'),('Step0','Soft'),('Step0','NormalTilt'),('Step0','BaseTint'),
       ('ShadowOutClear','ShadowOutBlocked'),('ShadowInClear','ShadowInBlocked'),
       ('LitShadowClear','LitShadowBlocked'),('SkyScale0','SkyScale1'),
       ('SkyLit','Dark'),('PointSpot','Point'),('PointSpot','Spot'),
       ('SurfaceBase','SurfaceRim'),('SurfaceRim','SurfaceHull'),
       ('Step0','RepeatStep0'),('SwitchLit','SwitchToon')]
results['differences']={f'{x}__{y}':diff(x,y) for x,y in pairs if x in data and y in data}
extra_pairs=[('StepLow','StepLowNoSelfShadow'),('Step0','Step0NoSelfShadow'),('Ext10Out','Ext10In'),
             ('Ext0Out','Ext0In'),('SpecR1','SpecSoft'),('SpecR1','SpecColor'),
             ('Point','LocalPointBlocked'),('Spot','LocalSpotBlocked'),('PointSpot','LocalBothBlocked'),
             ('SurfaceRimFinal','SurfaceScreen'),('GI0','GI1'),('GI1','GI2'),('GI1','GI3'),
             ('GI1','GIRepeat'),('GI3','GIRedRepeat'),('GIRepeat','GIRedRepeat')]
results['differences'].update({f'{x}__{y}':diff(x,y) for x,y in extra_pairs if x in data and y in data})
if 'SpecR0' in data:
    results['specular']={}
    for name in [n for n in data if n.startswith('Spec')]:
        q=lum(name)[mask];peak=float(q.max())
        results['specular'][name]={'peak_mean_rgb':peak,'pixels_above_half_peak':int((q>peak*.5).sum()),
            'rgb_at_brightest':data[name][mask][q.argmax()].tolist()}
if 'GIRepeat' in data:
    results['GI_RGB']={n:data[n][mask].mean(axis=0).tolist() for n in ('GI0','GI1','GI2','GI3','GI4','GI5','GIRepeat','GIRedRepeat')}
    chroma=lambda n:float((data[n][...,0]-data[n][...,1])[mask].mean())
    white_noise=abs(chroma('GI1')-chroma('GIRepeat'))
    red_noise=abs(chroma('GI3')-chroma('GIRedRepeat'))
    red_signal=abs(chroma('GIRedRepeat')-chroma('GIRepeat'))
    results['GI_chroma']={'metric':'mean(R-G) on fixed sphere mask; rejects achromatic shadow dither',
        'white_repeat_noise':white_noise,'red_repeat_noise':red_noise,'red_wall_signal':red_signal,
        'signal_over_max_repeat_noise':red_signal/max(white_noise,red_noise,1e-6)}
for prefix in ('Pair','Slot'):
    if prefix+'Before' in data:
        yy,xx=np.indices(mask.shape);region=data[prefix+'Before'].max(axis=2)>1e-5
        results[prefix.lower()+'_isolation']={side:diff(prefix+'Before',prefix+'After',region&sel)
            for side,sel in [('left',xx<128),('right',xx>=128)]}
if all(x in data for x in ('Point','Spot','PointSpot','Dark')):
    residual=data['PointSpot']-data['Point']-data['Spot']+data['Dark']
    results['local_light_additivity']={'relative_L1':float(np.abs(residual)[mask].sum()/max(np.abs(data['PointSpot'])[mask].sum(),1e-9))}
if 'PairBefore' in data:
    yy,xx=np.indices(mask.shape)
    # Pair silhouettes are offset; use static mask from nonzero pixels and split at image center.
    pairmask=(data['PairBefore'].max(axis=2)>1e-5)
    results['pair_update']={side:diff('PairBefore','PairAfter',pairmask&sel) for side,sel in [('left',xx<128),('right',xx>=128)]}
results['files']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(root.iterdir()) if f.suffix in ('.rgba32f','.png','.csv','.txt')}
results['checks']={
    'all_readbacks_finite':all(np.isfinite(x).all() for x in data.values()),
    'no_light_zero':float(np.abs(data['Dark']).max())<1e-6,
    'nonnegative_thresholds_within_dot_0_02':all(abs(results['thresholds'][n]['fitted_tau']-t)<.02 for n,t in [('Step0',0),('StepHigh',.5)]),
    'point_spot_additivity_L1_under_1_percent':results.get('local_light_additivity',{}).get('relative_L1',1)<.01,
    'sky_scale_has_effect':energy('SkyScale1')-energy('SkyScale0')>.05,
    'rim_visible':diff('SurfaceBase','SurfaceRim')>.01,
}
if 'SlotBefore' in data:
    results['checks']['two_slots_left_changes_right_stable']=results['slot_isolation']['left']>.05 and results['slot_isolation']['right']<.005
for name in ('SurfaceScreen','SurfaceHull'):
    if name in data:
        green=(data[name][...,1]>data[name][...,0]+.1)&(data[name][...,1]>data[name][...,2]+.1)
        results.setdefault('outline_green_pixels',{})[name]=int(green.sum())
        results['checks'][name+'_visible_green_outline']=bool(green.sum()>20)
if 'RGBNoSelfShadow' in data:
    rgb=data['RGBNoSelfShadow'];dom=rgb.argmax(axis=2)
    results['RGB_band_pixels']={c:int((mask&(dom==i)&(rgb.max(axis=2)>.05)).sum()) for i,c in enumerate(('R','G','B'))}
    results['checks']['three_RGB_bands_visible_without_self_shadow']=all(n>100 for n in results['RGB_band_pixels'].values())
if 'GI_chroma' in results:
    results['checks']['Lumen_indirect_scale_and_color_signal']=diff('GI0','GI1')>.005 and results['GI_chroma']['signal_over_max_repeat_noise']>5
if 'softness_positive' in results:
    results['checks']['one_linear_softness_fit_within_dot_0_02']=abs(results['softness_positive']['fitted_dot_half_width']-.15)<.02
# Preserve all original RGBA bytes losslessly in a portable compressed NumPy archive.
if archive is None:np.savez_compressed(root/'readbacks.npz',**rgba)
(root/'measurements.json').write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding='utf-8')
groups={
'01_diffuse':['StepLow','Step0','StepHigh','Soft','RGB','Continuous','Angle0','Angle3','Angle6'],
'02_shadow_lights':['ShadowOutClear','ShadowOutBlocked','ShadowInClear','ShadowInBlocked','LitShadowClear','LitShadowBlocked','Point','Spot','PointSpot','AllLights','SkyScale0','SkyScale1'],
'03_specular_surface':['SpecR0','SpecR1','SpecR2','SpecR3','SpecBlue','NormalTilt','BaseTint','SurfaceBase','SurfaceRim','SurfaceScreen','SurfaceHull'],
'04_runtime_gi':['PairBefore','PairAfter','SwitchLit','SwitchToon','RepeatStep0','GI0','GI1','GI2','GI3','GI4','GI5']}
groups['05_counterexamples']=['StepLow','StepLowNoSelfShadow','RGBNoSelfShadow','ShadowOutBlocked','ShadowInBlocked','Ext10Out','Ext10In','Ext0Out','Ext0In']
groups['06_slots_specular']=['SlotBefore','SlotAfter','SlotProfileSwitch','SpecR1','SpecSoft','SpecColor','SpecIntensityLow','SpecIntensityHigh','SurfaceRimFinal','SurfaceScreen','SurfaceHull']
for filename,names in groups.items():
    names=[n for n in names if (root/(n+'.png')).exists()]
    sheet=Image.new('RGB',(3*272,((len(names)+2)//3)*292),(235,237,239));draw=ImageDraw.Draw(sheet)
    for i,n in enumerate(names):
        x=(i%3)*272+8;y=(i//3)*292+28;sheet.paste(Image.open(root/(n+'.png')).convert('RGB'),(x,y));draw.text((x,y-21),n,fill=(20,30,35))
    sheet.save(root/(filename+'.jpg'),quality=92)
print(json.dumps({k:v for k,v in results.items() if k!='files'},indent=2))
