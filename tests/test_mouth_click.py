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
    assert any(e['start']<=digital<e['end'] for e in clicks['events']),'digital impulse missed'
    assert any(e['start']<last and e['end']>first for e in clicks['events']),'saliva proxy missed'
    breath=plan(source,'breaths',True,False);assert all(e['kind']==0 for e in breath['events'])
    mixed=plan(source,'both',True,True)
    assert [(e['start'],e['end']) for e in mixed['events'] if e['kind']==1]==[(e['start'],e['end']) for e in clicks['events']]
    run('plan',source,'--out',root/'neither.json','--breaths',0,'--clicks',0,ok=False)
    cleanplan=plan(cleanfile,'clean',False,True)
    trustworthy=[e for e in cleanplan['events'] if e['classification'] in ('digital_click','saliva_click')]
    assert not trustworthy,('clean transient falsely treated as reliable click',trustworthy)
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
        'duration_preserved':True,'outside_repair_exact':True,'overlap_once':True,'residual_verified':True}))
