#!/usr/bin/env python3
"""Small-budget native residual corrections over an unchanged v6 contour plan."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
import numpy as np
from scipy import ndimage as ndi

ROOT=Path(__file__).resolve().parent
# Repository layout first; standalone research checkout is an explicit fallback.
FLOW=ROOT.parent/'flow_planner'
LEGACY=ROOT.parent/'automatic_stages'
if not (FLOW/'flow.py').is_file():FLOW=ROOT.parent/'drawborad-flow-planner-next'
if not (LEGACY/'plan.py').is_file():LEGACY=ROOT.parent/'drawborad-planner-patch/tools/experimental/automatic_stages'
if not (FLOW/'flow.py').is_file() or not (LEGACY/'plan.py').is_file():
    raise ImportError('hybrid research requires sibling flow_planner and automatic_stages modules')
sys.path.insert(0,str(FLOW))
from flow import NativeCanvas,fit,exact

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
legacy=module('v6_palette_baseline',LEGACY/'plan.py')
contract=module('v6_input_contract',LEGACY/'contract.py')
MAX_EVENTS,MAX_SAMPLES,MAX_CORRECTIONS,MAX_SHORT_CORRECTIONS=2048,250000,192,0


def length(stroke):
    return sum(math.hypot(b[0]-a[0],b[1]-a[1]) for a,b in zip(stroke['geometry'],stroke['geometry'][1:]))


def native_baseline(image,masks):
    result=legacy.plan(image,masks,max_events=MAX_EVENTS,max_samples=MAX_SAMPLES)
    # v6's DBPAINT1 writer uses 12 significant digits. Match those actual input
    # values rather than quietly introducing different baseline geometry/colors.
    q=lambda x:float(f'{x:.12g}')
    for stroke in result['strokes']:
        stroke['width']=q(stroke['width']);stroke['color']=[q(v) for v in stroke['color']]
        stroke['geometry']=[tuple(q(v) for v in point) for point in stroke['geometry']]
        stroke.update(hardness=1.,opacity=1.,origin='unchanged-v6-baseline')
    return result


def isolated_source_mark(source,region,object_mask):
    ring=ndi.binary_dilation(region,structure=np.ones((3,3),bool))&object_mask&~region
    if int(region.sum())>9 or not ring.any():return False
    color=np.mean(source[:,:,:3][region],axis=0);neighbors=source[:,:,:3][ring]
    def features(rgb):
        return np.stack([rgb@np.array([.2126,.7152,.0722]),rgb[...,0]-rgb[...,1],rgb[...,2]-rgb[...,1]],axis=-1)
    center=features(color);surroundings=features(neighbors)
    low,high=surroundings.min(axis=0),surroundings.max(axis=0)
    # A real rare hue can lie between its neighbors in raw RGB but outside
    # their opponent-color range. Antialias mixtures stay inside that range.
    return bool(np.any((center<low-.12)|(center>high+.12)))


def correction_paths(region,color,reviewed_mask):
    paths=legacy.structure_paths(region,color,max_width=12,reviewed_mask=reviewed_mask)
    if int(region.sum())>=3:paths+=legacy.pigment_paths(region,color,max_width=12)
    return sorted(paths,key=lambda path:length(path)<3)


def correction_regions(source,masks,baseline,max_regions=96,max_seed_trials=512,report=None):
    error=np.max(np.abs(source[:,:,:3]-baseline[:,:,:3]/255),axis=2)
    mass=np.maximum(0.,ndi.uniform_filter(error*error,3,mode='nearest'))
    score=error*np.sqrt(mass)
    if not np.isfinite(score).all():raise ValueError('nonfinite residual candidate score')
    claimed=np.logical_or.reduce([m>0 for m in masks])
    score[~claimed]=0
    visited=np.zeros(claimed.shape,bool);regions=[];trials=0;untraceable=0
    owner=np.full(claimed.shape,-1,int)
    for oi,m in enumerate(masks):owner[m>0]=oi
    # Visit high-error connected source-color regions, not an arbitrary fixed
    # pixel grid. Low contrast antialias residuals do not each get an event.
    for trial in range(max_seed_trials):
        candidates=np.where((error>.07)&~visited,score,0)
        if candidates.max()<.004:break
        trials+=1
        y,x=np.unravel_index(candidates.argmax(),candidates.shape);oi=int(owner[y,x])
        if oi<0:break
        seed=source[y,x,:3]
        color_support=(masks[oi]>0)&(np.max(np.abs(source[:,:,:3]-seed),axis=2)<.075)
        components,_=ndi.label(color_support,structure=np.ones((3,3),np.uint8))
        region=components==components[y,x]
        visited[region]=True
        area=int(region.sum())
        color=np.r_[np.mean(source[:,:,:3][region],axis=0),1.]
        true_dot=isolated_source_mark(source,region,masks[oi]>0)
        if area<3 or (area<12 and not any(length(path)>=3 for path in correction_paths(region,color,masks[oi]>0))):
            untraceable+=1;continue
        # Count the measured residual mass, not the supposed identity of a mark.
        value=float(np.sum(error[region]**2))
        if value<.025:continue
        regions.append(dict(object=oi,mask=region,color=color,area=area,value=value,isolatedExtremum=true_dot))
        regions.sort(key=lambda r:(-r['value'],r['object']))
        if len(regions)>max_regions:regions.pop()
    regions.sort(key=lambda r:(-r['value'],r['object']))
    if report is not None:report.update(maxSeedTrials=max_seed_trials,maxRetainedRegions=max_regions,seedTrials=trials,rejectedUntraceableRegions=untraceable,retainedRegions=len(regions))
    return regions


def validated_inputs(image,masks,diagnostic_outside_base_contract=False):
    report=contract.preflight(image,masks)
    # Diagnostics can inspect opaque gradients/photos, not malformed buffers,
    # overlapping authorization masks, or known-unsupported soft alpha.
    blockers=[r for r in report['rejectionReasons'] if not
              (diagnostic_outside_base_contract and
               (r.endswith(':palette-too-distributed') or r.endswith(':smooth-gradient-content')))]
    if blockers:raise ValueError('unsupported hybrid input: '+', '.join(blockers))
    return np.ascontiguousarray(image),[np.ascontiguousarray(m) for m in masks]


def plan(image,masks,diagnostic_outside_base_contract=False):
    image,masks=validated_inputs(image,masks,diagnostic_outside_base_contract)
    if np.any(np.logical_or.reduce([m>0 for m in masks])&(image[:,:,3]!=255)):raise ValueError('opaque reviewed pixels required')
    baseline=native_baseline(image,masks);strokes=list(baseline['strokes']);samples=sum(len(s['geometry']) for s in strokes)
    target=image.copy();target[:,:,3]*=np.logical_or.reduce([m>0 for m in masks])
    canvas=NativeCanvas(target);previous='base'
    for stroke in strokes:
        if stroke['phase']!=previous:canvas.phase();previous=stroke['phase']
        canvas.candidate(stroke,masks[stroke['object']]);gain=canvas.evaluate(stroke['color'],1.)
        if gain<-1e20:raise ValueError('baseline native feedback rejected')
        canvas.commit()
    base_pixels=canvas.pixels
    if previous!='detail':canvas.phase()
    source=image.astype(float)/255;search={}
    regions=correction_regions(source,masks,base_pixels,report=search)
    corrections=[];attempts=0;selected=[];short_corrections=0
    for region in regions:
        if len(corrections)>=MAX_CORRECTIONS or len(strokes)>=MAX_EVENTS:break
        oi=region['object'];mask=region['mask'];color=region['color']
        # Centerline strokes provide coherent variable-width coverage. Contour
        # candidates supply corners that a medial path alone can miss.
        proposed=correction_paths(mask,color,masks[oi]>0)
        accepted=0
        for stroke in proposed:
            if len(corrections)>=MAX_CORRECTIONS or len(strokes)>=MAX_EVENTS:break
            short=length(stroke)<3
            if short:continue
            if samples+len(stroke['geometry'])>MAX_SAMPLES:continue
            stroke.update(object=oi,phase='detail',hardness=1.,origin='native-residual-local-color',isolatedExtremum=region['isolatedExtremum'])
            attempts+=1
            if fit(canvas,target,stroke,masks[oi],.01):
                strokes.append(stroke);corrections.append(stroke);samples+=len(stroke['geometry']);accepted+=1
                short_corrections+=int(short)
        if accepted:selected.append(dict(object=oi,sourceRegionArea=region['area'],initialResidualMass=region['value'],acceptedEvents=accepted,isolatedExtremum=region['isolatedExtremum']))
    final=canvas.pixels;inside=np.logical_or.reduce([m>0 for m in masks])
    error=lambda a:float(np.mean(np.abs(a[:,:,:3].astype(float)-image[:,:,:3])[inside])/255)
    result=dict(schema='DBHYBRID1',width=image.shape[1],height=image.shape[0],objects=len(masks),strokes=strokes,baselineEvents=len(baseline['strokes']),addedEvents=len(corrections),shortAddedEvents=short_corrections,samples=samples,candidateEvents=attempts,candidateSearch=search,correctedSourceRegions=selected,beforeRgbMae=error(base_pixels),afterRgbMae=error(final),budget=dict(maxEvents=MAX_EVENTS,maxSamples=MAX_SAMPLES,maxCorrections=MAX_CORRECTIONS,maxShortCorrections=MAX_SHORT_CORRECTIONS),qualityStatus='not-evaluated',naturalProcessVerified=False)
    return result,base_pixels,final


def write_plan(result,output):
    """Declared prefix boundary; native checks corrections, not v6 authenticity."""
    baseline=result['baselineEvents'];count=len(result['strokes'])
    if type(baseline) is not int or not 1<=baseline<=count<=MAX_EVENTS or count-baseline>MAX_CORRECTIONS:
        raise ValueError('hybrid baseline/correction budget')
    output.mkdir(parents=True,exist_ok=False)
    (output/'plan.json').write_text(json.dumps(result,separators=(',',':')))
    with (output/'plan.txt').open('w') as f:
        f.write(f"DBHYBRID1 {result['width']} {result['height']} {result['objects']} {count} {baseline}\n")
        for s in result['strokes']:
            f.write(f"{s['object']} {legacy.PHASES.index(s['phase'])} {s['width']:.17g} {s['hardness']:.17g} {s['opacity']:.17g} "+' '.join(f'{v:.17g}' for v in s['color'])+f" {len(s['geometry'])}\n")
            for x,y,p in s['geometry']:f.write(f'{x:.17g} {y:.17g} {p:.17g}\n')


def main():
    p=argparse.ArgumentParser();p.add_argument('--rgba',type=Path,required=True);p.add_argument('--width',type=int,required=True);p.add_argument('--height',type=int,required=True);p.add_argument('--mask',type=Path,action='append',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--research',action='store_true');p.add_argument('--diagnostic-outside-base-contract',action='store_true')
    args=p.parse_args()
    if not args.research:p.error('explicit --research required; no stable default changes')
    if not 1<=args.width<=512 or not 1<=args.height<=512 or not 1<=len(args.mask)<=16:p.error('image/object budget')
    if args.output.exists():p.error('output exists; no clobber')
    image=np.frombuffer(exact(args.rgba,args.width*args.height*4),np.uint8).reshape(args.height,args.width,4)
    masks=[np.frombuffer(exact(m,args.width*args.height),np.uint8).reshape(args.height,args.width).copy() for m in args.mask]
    report=contract.preflight(image,masks)
    if not report['eligible'] and not args.diagnostic_outside_base_contract:
        print(json.dumps(report));raise SystemExit(3)
    result,baseline,final=plan(image,masks,args.diagnostic_outside_base_contract);result['baseInputContract']=report
    write_plan(result,args.output);baseline.tofile(args.output/'baseline.rgba');final.tofile(args.output/'feedback.rgba')
    print(json.dumps({key:result[key] for key in ['baselineEvents','addedEvents','samples','candidateEvents','beforeRgbMae','afterRgbMae']}),flush=True)
if __name__=='__main__':main()
