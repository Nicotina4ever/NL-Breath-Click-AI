# SPDX-License-Identifier: MPL-2.0
"""Reference DSP regressions. Synthetic phonetic proxies are not real saliva validation."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np
import soundfile as sf

p=argparse.ArgumentParser();p.add_argument('--cli',type=Path,required=True);a=p.parse_args();cli=a.cli.resolve()
def run(*args,ok=True):
    r=subprocess.run([str(cli),*map(str,args)],capture_output=True,text=True,encoding='utf-8')
    assert (r.returncode==0)==ok,(args,r.stdout,r.stderr)
    return r
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
with tempfile.TemporaryDirectory(prefix='nlbc_mouth_') as tmp:
    root=Path(tmp);sr=48000;t=np.arange(sr*2)/sr
    clean=(.08*np.sin(2*np.pi*223*t)+.025*np.sin(2*np.pi*446*t)).astype(np.float32)
    for start,frequency in ((1.3,95),(1.6,6200)):
        first=int(start*sr);n=int(.025*sr)
        clean[first:first+n]+=(.02*np.hanning(n)*np.sin(2*np.pi*frequency*np.arange(n)/sr)).astype(np.float32)
    noisy=clean.copy();digital=int(.35*sr);noisy[digital]+=.6
    first,last=int(.8*sr),int(.803*sr)
    noisy[first:last]+=(np.random.default_rng(981).normal(0,.08,last-first)*np.hanning(last-first)).astype(np.float32)
    source=root/'synthetic_click.wav';sf.write(source,noisy,sr,subtype='FLOAT');before=sha(source)
    cleanfile=root/'synthetic_clean.wav';sf.write(cleanfile,clean,sr,subtype='FLOAT')
    def plan(source,name,breaths,clicks,detector='dsp',extra=()):
        target=root/(name+'.json')
        run('plan',source,'--out',target,'--breaths',int(breaths),'--clicks',int(clicks),'--detector',detector,*extra)
        return json.loads(target.read_text(encoding='utf-8'))
    clicks=plan(source,'clicks',False,True,'fusion',('--model',root/'absent.onnx'))
    assert clicks['ai_inference_frames']==0 and clicks['events'],clicks
    assert all(e['kind']==1 for e in clicks['events'])
    # Band-limited ringing can have a small one-sample residual even when
    # audible. Wider predictors must expose it, without auto-approving it.
    for sample_rate in (44100,48000,96000):
        time=np.arange(sample_rate)/sample_rate
        background=(.00005*np.sin(2*np.pi*137*time)).astype(np.float32)
        softer=background.copy();lo=int(.5*sample_rate);size=int(.005*sample_rate)
        softer[lo:lo+size]+=(.003*np.hanning(size)*np.sin(2*np.pi*3000*np.arange(size)/sample_rate)).astype(np.float32)
        source_soft=root/f'soft_{sample_rate}.wav';sf.write(source_soft,softer,sample_rate,subtype='FLOAT')
        soft=plan(source_soft,f'soft_{sample_rate}',False,True)
        found=[e for e in soft['events'] if e['classification']=='band_limited_click' and e['start']<lo+size and e['end']>lo]
        assert found and all(not e['approved'] and not e['training_eligible'] for e in found),(sample_rate,soft)
        output=root/f'soft_output_{sample_rate}'
        run('process',source_soft,'--breaths',0,'--clicks',1,'--out-dir',output)
        untouched,rate=sf.read(output/(source_soft.stem+'_breathclick.wav'),dtype='float32')
        assert rate==sample_rate and np.array_equal(untouched,softer),'new review candidate automatically edited'
    assert any(e['start']<=digital<e['end'] for e in clicks['events']),'digital impulse missed'
    # A quiet broader-band click can be too small for the one-sample floor,
    # while both wider predictors independently isolate it above the context.
    broad=clean[:sr].copy()*0
    at=sr//2;size=round(sr*.004)
    broad[at:at+size]=(.00045*np.hanning(size)*np.sin(2*np.pi*5000*np.arange(size)/sr)).astype(np.float32)
    src_broad=root/'broad_quiet.wav';sf.write(src_broad,broad,sr,subtype='FLOAT')
    quiet=plan(src_broad,'broad_quiet',False,True)
    assert any(e['classification']=='isolated_click_review' and e['start']<at+size and e['end']>at for e in quiet['events'])
    assert all(not e['approved'] and not e['training_eligible'] for e in quiet['events'])
    # A release after a much stronger syllable must not enter this quiet-gap pass.
    phonetic=broad.copy();vowel=slice(at-round(sr*.12),at-round(sr*.05))
    phonetic[vowel]=(.02*np.sin(2*np.pi*170*np.arange(vowel.stop-vowel.start)/sr)).astype(np.float32)
    src_release=root/'phonetic_release.wav';sf.write(src_release,phonetic,sr,subtype='FLOAT')
    release=plan(src_release,'phonetic_release',False,True)
    assert not any(e['classification']=='isolated_click_review' and e['start']<at+size and e['end']>at for e in release['events'])
    assert any(e['start']<last and e['end']>first for e in clicks['events']),'saliva proxy missed'
    breath=plan(source,'breaths',True,False);assert all(e['kind']==0 for e in breath['events'])
    mixed=plan(source,'both',True,True)
    assert [(e['start'],e['end']) for e in mixed['events'] if e['kind']==1]==[(e['start'],e['end']) for e in clicks['events']]
    run('plan',source,'--out',root/'neither.json','--breaths',0,'--clicks',0,ok=False)
    cleanplan=plan(cleanfile,'clean',False,True)
    trustworthy=[e for e in cleanplan['events'] if e['classification'] in ('digital_click','saliva_click')]
    assert not trustworthy,('clean transient falsely treated as reliable click',trustworthy)
    weak=np.zeros(sr,dtype=np.float32);weak[sr//2]=.0005
    weakfile=root/'weak.wav';sf.write(weakfile,weak,sr,subtype='FLOAT')
    weakplan=plan(weakfile,'weak',False,True)
    assert any(e['start']<=sr//2<e['end'] and e['classification']=='low_level_impulse' and not e['approved'] for e in weakplan['events']),weakplan
    out=root/'weak_automatic';run('process',weakfile,'--out-dir',out,'--breaths',0,'--clicks',1)
    untreated,rate=sf.read(out/'weak_breathclick.wav',dtype='float32')
    assert rate==sr and np.array_equal(untreated,weak),'weak review candidate was edited automatically'
    weak_report=json.loads((out/'weak_breathclick.wav.report.json').read_text())
    assert weak_report['click_decisions'] and all(o['outcome']=='review' and not o['applied'] and not o['declined'] for o in weak_report['click_decisions'])
    def render(method,duplicate=False,residual=False):
        spec=dict(clicks)
        event={'kind':1,'start':first,'end':last,'enabled':True,'approved':True,'mode':'attenuate','repair':True,'repair_method':method,'fade_ms':0}
        spec['events']=[event,event.copy()] if duplicate else [event]
        path=root/(method+('_dup' if duplicate else '')+'.json');path.write_text(json.dumps(spec),encoding='utf-8')
        out=root/(method+('_dup' if duplicate else '')+('_residual' if residual else ''));out.mkdir()
        run('difference' if residual else 'edit',source,'--plan',path,'--out-dir',out)
        output=next(out.glob('*.wav'));data,rate=sf.read(output,dtype='float32')
        assert rate==sr and len(data)==len(clean) and sha(source)==before
        return data,path
    linear,_=render('linear');predictive,path=render('predictive')
    before_mse=float(np.mean((noisy[first:last]-clean[first:last])**2))
    linear_mse=float(np.mean((linear[first:last]-clean[first:last])**2))
    predictive_mse=float(np.mean((predictive[first:last]-clean[first:last])**2))
    assert predictive_mse<before_mse*.05,(before_mse,predictive_mse)
    assert predictive_mse<linear_mse*.25,(linear_mse,predictive_mse)
    assert np.array_equal(predictive[:first],noisy[:first]) and np.array_equal(predictive[last:],noisy[last:])
    duplicate,_=render('predictive',True);assert np.array_equal(duplicate,predictive),'overlap applied more than once'
    residual,_=render('predictive',residual=True);assert np.max(np.abs(residual-(noisy-predictive)))<1e-7
    spec=json.loads(path.read_text());spec['events'][0]['mode']='halve';path.write_text(json.dumps(spec))
    run('edit',source,'--plan',path,'--out-dir',root/'invalid',ok=False)
    labelled=dict(clicks)
    labelled['events']=[dict(e,learning_label='click_false_positive',training_eligible=True,decision_source='user',manual_operation=False) for e in clicks['events']]
    labels=root/'click_labels.json';labels.write_text(json.dumps(labelled),encoding='utf-8')
    exported=root/'click_session.json';run('labels-export','--plan',labels,'--out',exported)
    session=json.loads(exported.read_text(encoding='utf-8'))
    assert session['regions'] and all(r['kind']==1 and r['learning_label']=='click_false_positive' for r in session['regions'])
    trained=run('profile-train','--plan',labels,'--out',root/'must_not_train.ini',ok=False)
    assert '0 e 0' in trained.stderr,'click labels leaked into breath calibrator'
    # Automatic selector must demonstrate both real branches on separate known
    # references. A safer no-op alone is not evidence of useful click repair.
    auto,_=render('auto')
    auto_report=json.loads((root/'auto/synthetic_click_edit.wav.edit.json').read_text())
    decision=auto_report['operations'][0]['click_strategy']
    assert decision['selected']=='predictive' and decision['predictive_validation_error']<=.1
    assert np.mean((auto[first:last]-clean[first:last])**2)<before_mse*.05
    straight=np.linspace(-.05,.05,len(noisy),dtype=np.float32);corrupt=straight.copy();corrupt[digital]+=.6
    ramp=root/'ramp.wav';sf.write(ramp,corrupt,sr,subtype='FLOAT')
    spec=dict(clicks);spec['events']=[dict(kind=1,start=digital-20,end=digital+21,enabled=True,approved=True,mode='attenuate',repair=True,repair_method='auto')]
    path=root/'ramp.json';path.write_text(json.dumps(spec));out=root/'ramp';out.mkdir()
    run('edit',ramp,'--plan',path,'--out-dir',out)
    d=json.loads((out/'ramp_edit.wav.edit.json').read_text())['operations'][0]['click_strategy']
    assert d['selected']=='linear' and d['linear_voice_risk']<=.1 and d['requested']=='auto',d
    # A different phoneme 8 ms away must not be mistaken for an onset at the
    # repair boundary. Conversely a real local level transition remains guarded.
    level=np.full(len(noisy),.005,dtype=np.float32)
    level[digital-1200:digital-384]=.15
    level[digital]+=.6
    distant=root/'distant.wav';sf.write(distant,level,sr,subtype='FLOAT')
    path=root/'distant.json';path.write_text(json.dumps(spec));out=root/'distant';out.mkdir()
    run('edit',distant,'--plan',path,'--out-dir',out)
    result=json.loads((out/'distant_edit.wav.edit.json').read_text())
    d=result['operations'][0]['click_strategy']
    assert result['applied_events']==1 and d['context_level_ratio']>8 and d['boundary_level_ratio']<2,d
    level[digital+21:]=.15
    onset=root/'onset.wav';sf.write(onset,level,sr,subtype='FLOAT')
    out=root/'onset';out.mkdir();run('edit',onset,'--plan',path,'--out-dir',out)
    result=json.loads((out/'onset_edit.wav.edit.json').read_text())
    unchanged,_=sf.read(out/'onset_edit.wav',dtype='float32')
    assert result['declined_events']==1 and np.array_equal(unchanged,level),result
    assert decision['predictive_voice_risk']<decision['linear_voice_risk']*.8,decision
    assert decision['predictive_bridge_error']>=0 and decision['predictive_voice_risk']<=.1,decision
    # RC3 listening regression: a forced 553-sample (11.52 ms) speech-overlap
    # region erased a voice fragment with both Linear and Burg. Choosing a
    # manual strategy must not bypass the voice-reconstruction safety budget.
    # No private waveform or listening preference is part of this fixture.
    gap_start=sr//2;gap_end=gap_start+553
    speech=(.04*np.sin(2*np.pi*173*t)+np.random.default_rng(553).normal(0,.018,len(t))).astype(np.float32)
    # Stereo rejection must be atomic, even if the first channel is predictable.
    unsafe=np.column_stack((np.full(len(t),.02,dtype=np.float32),speech))
    widefile=root/'wide_speech.wav';sf.write(widefile,unsafe,sr,subtype='FLOAT')
    for method in ('linear','predictive','auto'):
        wide=dict(clicks,channels=2,source=str(widefile),source_relative='',source_sha256=sha(widefile))
        wide['events']=[dict(kind=1,start=gap_start,end=gap_end,enabled=True,
            approved=True,repair=True,repair_method=method,mode='attenuate',classification='speech_overlap')]
        path=root/('wide_'+method+'.json');path.write_text(json.dumps(wide))
        out=root/('wide_'+method);out.mkdir();run('edit',widefile,'--plan',path,'--out-dir',out)
        y,rate=sf.read(out/'wide_speech_edit.wav',dtype='float32')
        report=json.loads((out/'wide_speech_edit.wav.edit.json').read_text())
        assert report['declined_events']==1 and report['applied_events']==0,(method,report)
        assert report['operations'][0]['outcome']=='preserved_safety'
        assert report['event_outcomes'][0]['outcome']=='preserved_safety'
        assert rate==sr and np.array_equal(y,unsafe),'unsafe manual repair erased or partially changed voice'
        if method!='auto':assert report['operations'][0]['click_strategy']['reason']=='unsafe_manual_reconstruction'
        # An equally wide, actually predictable gap must still be repairable:
        # this is a reconstruction check, not a blanket duration restriction.
        safe=np.full(len(t),.02,dtype=np.float32);safe[gap_start+200]+=.6
        safefile=root/('wide_safe_'+method+'.wav');sf.write(safefile,safe,sr,subtype='FLOAT')
        wide.update(channels=1,source=str(safefile),source_sha256=sha(safefile));path.write_text(json.dumps(wide))
        out=root/('wide_safe_'+method);out.mkdir();run('edit',safefile,'--plan',path,'--out-dir',out)
        repaired,rate=sf.read(out/(safefile.stem+'_edit.wav'),dtype='float32')
        report=json.loads((out/(safefile.stem+'_edit.wav.edit.json')).read_text())
        assert report['applied_events']==1 and report['declined_events']==0,(method,report)
        assert report['operations'][0]['outcome']=='repaired'
        assert np.max(np.abs(repaired[gap_start:gap_end]-.02))<1e-6,'valid wide click reconstruction lost efficacy'
        assert np.array_equal(repaired[:gap_start],safe[:gap_start]) and np.array_equal(repaired[gap_end:],safe[gap_end:])
    # Short explicit edge interpolations still work without 25 ms of context.
    edge=np.full(len(t),.02,dtype=np.float32);edge[3]+=.6;edge[-4]+=.6
    edgefile=root/'edges.wav';sf.write(edgefile,edge,sr,subtype='FLOAT')
    edgeplan=dict(clicks,source=str(edgefile),source_relative='',source_sha256=sha(edgefile))
    edgeplan['events']=[dict(kind=1,start=lo,end=hi,enabled=True,approved=True,repair=True,
        repair_method='linear',mode='attenuate') for lo,hi in ((1,6),(len(edge)-6,len(edge)-1))]
    path=root/'edges.json';path.write_text(json.dumps(edgeplan));out=root/'edges';out.mkdir()
    run('edit',edgefile,'--plan',path,'--out-dir',out)
    edge_repaired,rate=sf.read(out/'edges_edit.wav',dtype='float32')
    assert rate==sr and len(edge_repaired)==len(edge) and np.max(np.abs(edge_repaired-.02))<1e-7
    # Even an admissible short Linear bridge should yield to Burg when the
    # identical hidden-sample test predicts substantially lower voice loss.
    periodic=clean.copy();periodic[digital]+=.6
    periodicfile=root/'periodic.wav';sf.write(periodicfile,periodic,sr,subtype='FLOAT')
    path=root/'periodic.json';path.write_text(json.dumps(spec));out=root/'periodic';out.mkdir()
    run('edit',periodicfile,'--plan',path,'--out-dir',out)
    d=json.loads((out/'periodic_edit.wav.edit.json').read_text())['operations'][0]['click_strategy']
    assert d['selected']=='predictive' and d['linear_voice_risk']<=.1,d
    repaired,_=sf.read(out/'periodic_edit.wav',dtype='float32')
    assert np.mean((repaired[digital-20:digital+21]-clean[digital-20:digital+21])**2)<1e-7
    # None is an explicit bypass, not an attenuation or an automatic refusal.
    spec['events'][0].update(repair_method='none',learning_label='click_confirmed',training_eligible=True,decision_source='user')
    path=root/'none.json';path.write_text(json.dumps(spec));out=root/'none';out.mkdir()
    run('edit',ramp,'--plan',path,'--out-dir',out)
    bypass,_=sf.read(out/'ramp_edit.wav',dtype='float32')
    r=json.loads((out/'ramp_edit.wav.edit.json').read_text());op=r['operations'][0]
    assert np.array_equal(bypass,corrupt) and r['applied_events']==r['declined_events']==0
    assert op['bypassed'] and op['click_strategy']['reason']=='manual_bypass'
    assert op['outcome']=='bypassed' and r['event_outcomes'][0]['outcome']=='bypassed'
    # Pending review is observable even though it has no attempted operation.
    spec['events'][0].update(approved=False,decision_source='none',learning_label='',training_eligible=False)
    path=root/'review.json';path.write_text(json.dumps(spec));out=root/'review';out.mkdir()
    run('edit',ramp,'--plan',path,'--out-dir',out)
    r=json.loads((out/'ramp_edit.wav.edit.json').read_text())
    assert r['applied_events']==r['declined_events']==0 and not r['operations']
    assert r['event_outcomes'][0]['outcome']=='review'
    assert abs(op['click_strategy']['duration_ms']-1000*41/sr)<1e-6
    run('labels-export','--plan',root/'none.json','--out',root/'none_labels.json')
    assert json.loads((root/'none_labels.json').read_text())['regions'][0]['learning_label']=='click_confirmed'
    # A successful first-channel prediction must not modify either channel if
    # another channel is unpredictable. Batch still repairs the separate impulse.
    stereo=np.column_stack((noisy,np.random.default_rng(73).normal(0,.01,len(noisy)).astype(np.float32)))
    stereo[digital,1]+=.6
    sf.write(source,stereo,sr,subtype='FLOAT')
    safe=plan(source,'risk',False,True)
    safe['events']=[dict(kind=1,start=first,end=last,enabled=True,approved=True,repair=True,mode='attenuate',repair_method='auto'),
                    dict(kind=1,start=digital-2,end=digital+3,enabled=True,approved=True,repair=True,mode='attenuate',repair_method='auto')]
    path=root/'risk.json';path.write_text(json.dumps(safe));out=root/'risk';out.mkdir()
    run('edit',source,'--plan',path,'--out-dir',out)
    y,_=sf.read(out/'synthetic_click_edit.wav',dtype='float32')
    report=json.loads((out/'synthetic_click_edit.wav.edit.json').read_text())
    assert report['declined_events']==1 and report['applied_events']==1 and report['rejected_events']==0
    assert np.array_equal(y[first:last],stereo[first:last]),'unsafe auto prediction partially modified stereo'
    assert abs(y[digital,0]-stereo[digital,0])>.5,'safe batch impulse was not repaired'
    print(json.dumps({'status':'passed','fixtures':'synthetic only; no claim about real saliva accuracy',
        'click_candidates':len(clicks['events']),'clean_review_candidates':len(cleanplan['events']),
        'clean_reliable_false_positives':len(trustworthy),'injected_events_found':2,
        'repair_mse':{'before':before_mse,'linear':linear_mse,'predictive':predictive_mse},
        'duration_preserved':True,'outside_repair_exact':True,'overlap_once':True,'residual_verified':True,
        'manual_unsafe_voice_preserved':True,'predictable_wide_click_repaired':True,'short_edge_repairs_preserved':True}))
