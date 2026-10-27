# SPDX-License-Identifier: MPL-2.0
"""Execute native slice/plan/render and the actual background worker on synthetic media."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import numpy as np
import soundfile as sf

repo = Path(__file__).resolve().parents[2]
exe = Path(os.environ['BC_TEST_BIN']) / ('bc_studio_cli.exe' if os.name=='nt' else 'bc_studio_cli')
worker = repo / 'reaper' / 'bc_job.ps1'

def call(*args, ok=True):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, timeout=40)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

with tempfile.TemporaryDirectory(prefix='bc_reaper_bridge_') as temp:
    capabilities=json.loads(call('capabilities').stdout)
    assert capabilities['reaper_bridge']==1 and capabilities['direct_editing']==2 and capabilities['personal_profiles']==1
    root = Path(temp) / 'caffè 🎙'; root.mkdir()
    rate = 48000
    t = np.arange(rate * 3) / rate
    samples = np.stack((.2*np.sin(2*np.pi*330*t), .1*np.sin(2*np.pi*220*t)), axis=1).astype('float32')
    for extension, subtype in [('wav', 'FLOAT'), ('flac', 'PCM_24'), ('mp3', 'MPEG_LAYER_III')]:
        source = root / f'source.{extension}'
        sf.write(source, samples, rate, subtype=subtype)
        before = sha(source)
        segment = root / f'{extension}_segment.wav'
        call('slice', source, '--out', segment, '--start', '.25', '--duration', '1.5')
        audio, sr = sf.read(segment, dtype='float32', always_2d=True)
        reference, _ = sf.read(source, dtype='float32', always_2d=True)
        assert sr == rate and audio.shape == (72000, 2)
        # Different MP3 decoders need bounded tolerance; WAV crop must be exact.
        delta = np.max(np.abs(audio-reference[12000:84000]))
        assert delta <= (0 if extension == 'wav' else .005), (extension, delta)
        meta = json.loads(Path(str(segment)+'.source.json').read_text())
        assert meta['source_sha256'] == before and meta['source_start_frame'] == 12000
        call('slice', source, '--out', source, '--start', '0', '--duration', '1', ok=False)
        call('slice', source, '--out', segment, '--start', '0', '--duration', '1', ok=False)
        for offset, duration in [('nan', '1'), ('0', 'inf'), ('-1', '1'), ('2.5', '2')]:
            call('slice', source, '--out', root/'invalid.wav', '--start', offset, '--duration', duration, ok=False)
        call('verify-source', source, '--sha256', before)
        call('verify-source', source, '--sha256', '0'*64, ok=False)
        assert sha(source) == before
    # Full real worker, including safe Windows argv paths and a genuine hash conflict.
    source = root/'source.wav'
    jobroot = root/'analysis'; jobroot.mkdir()
    request = dict(mode='analyze', exe=str(exe), source=str(source), offset=.25, duration=1.5,
                   detector='dsp', reduction_db=-12, fade_ms=15)
    def run_job(folder, data, cancel=False):
        path = folder/'request.json'; path.write_text(json.dumps(data), encoding='utf-8')
        if cancel: (folder/'cancel').touch()
        subprocess.run(([sys.executable,str(repo/'reaper/bc_job.py'),'--request',str(path)] if os.name!='nt' or os.environ.get('BC_PORTABLE_WORKER')=='1' else ['powershell','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(worker),'-Request',str(path)]),check=True,capture_output=True,timeout=45)
        return json.loads((folder/'result.json').read_text(encoding='utf-8-sig'))
    result = run_job(jobroot, request)
    assert result['status'] == 'complete', result
    plan = json.loads(Path(result['plan']).read_text(encoding='utf-8-sig'))
    plan['events'] = [dict(kind=0, start=12000, end=24000, mode='attenuate', enabled=True,
                           approved=True, state='manual', manual_operation=True, decision_source='user',
                           training_eligible=False, fade_ms=0, keep_fraction=.5)]
    for mode, frames in [('attenuate',72000),('silence',72000),('cut',60000),('halve',66000)]:
        folder = root/mode; folder.mkdir()
        plan['events'][0]['mode'] = mode
        path = folder/'reviewed.json'; path.write_text(json.dumps(plan), encoding='utf-8')
        output = run_job(folder, dict(mode='render',exe=str(exe),source=str(source),source_sha256=sha(source),
                                     prepared=result['prepared'],plan=str(path)))
        assert output['status'] == 'complete', output
        edited, _ = sf.read(output['output'], always_2d=True)
        assert len(edited) == frames, (mode, len(edited))
        if mode in ('attenuate','silence'):
            base, _ = sf.read(result['prepared'], always_2d=True)
            assert np.array_equal(edited[:12000], base[:12000]) and np.array_equal(edited[24000:],base[24000:])
            if mode=='silence': assert np.max(np.abs(edited[12000:24000])) == 0
    cancelled = root/'cancelled'; cancelled.mkdir()
    assert run_job(cancelled, request, cancel=True)['status'] == 'cancelled'
    stale = root/'stale'; stale.mkdir()
    before = sha(source)
    stale_result = run_job(stale, dict(mode='render',exe=str(exe),source=str(source),source_sha256='0'*64,
                                     prepared=result['prepared'],plan=str(path)))
    assert stale_result['status'] == 'failed' and not (stale/'input_edit.wav').exists()
    assert sha(source) == before
    # Preserve-sync shortens only inside each event and inserts a pause at its
    # removed span. Following PCM and all absolute cues remain sample exact.
    for mode in ('cut','halve'):
        folder=root/f'sync_{mode}';folder.mkdir()
        plan['preserve_duration']=True
        plan['fade_ms']=0
        plan['events'][0].update(mode=mode,fade_ms=0)
        p=folder/'plan.json';p.write_text(json.dumps(plan),encoding='utf-8')
        output=run_job(folder,dict(mode='render',exe=str(exe),source=str(source),source_sha256=sha(source),prepared=result['prepared'],plan=str(p)))
        assert output['status']=='complete',output
        audio,_=sf.read(output['output'],always_2d=True)
        base,_=sf.read(result['prepared'],always_2d=True)
        assert len(audio)==len(base)
        assert np.array_equal(audio[:12000],base[:12000]) and np.array_equal(audio[24000:],base[24000:])
        first,last=(12000,24000) if mode=='cut' else (15000,21000)
        assert np.count_nonzero(audio[first:last])==0
        if mode=='halve':
            assert np.array_equal(audio[12000:15000],base[12000:15000])
            assert np.array_equal(audio[21000:24000],base[21000:24000])
        report=json.loads(Path(output['report']).read_text(encoding='utf-8-sig'))
        assert report['edited_frames']==72000 and report['removed_samples']==0
    # Full original recording identity, not cache/crop identity, defines the
    # independent split. Exported explicit labels open in standalone sessions.
    call('plan',source,'--out',root/'learn-base.json')
    fullplan=json.loads((root/'learn-base.json').read_text(encoding='utf-8-sig'))
    fullplan['events']=[]
    for i in range(6):
        positive=i%2==0
        fullplan['events'].append(dict(kind=0,start=i*16000,end=i*16000+8000,mode='attenuate',score=.99 if positive else .01,
            approved=positive,enabled=positive,state='confirmed' if positive else 'rejected',
            learning_label='breath_confirmed' if positive else 'false_positive',decision_source='user',training_eligible=True))
    # Lua and other clients may emit the events first: nested source/gain
    # fields must never shadow the plan's root fields.
    fullplan['events'][0]['source']='ai'
    fullplan=dict(events=fullplan.pop('events'),**fullplan)
    labels1=root/'labels1.json';labels1.write_text(json.dumps(fullplan),encoding='utf-8')
    original_hash=sha(source)
    call('labels-export','--plan',labels1,'--out',source,ok=False)
    assert sha(source)==original_hash
    session=root/'labels.bcnstudio.json'
    call('labels-export','--plan',labels1,'--out',session)
    restored=json.loads(session.read_text(encoding='utf-8-sig'))
    assert len(restored['regions'])==6 and all(r['training_eligible'] for r in restored['regions'])
    profile=root/'personal_profile.ini'
    call('profile-train','--plan',labels1,'--plan',labels1,'--out',profile,ok=False)
    assert not profile.exists(), 'Copies/crops of one recording cannot become independent validation'
    copied=root/'renamed.wav';sf.write(copied,samples,rate,subtype='FLOAT')
    copy_plan=dict(fullplan,source=str(copied),source_relative='',source_sha256=sha(copied))
    copy_file=root/'renamed.json';copy_file.write_text(json.dumps(copy_plan),encoding='utf-8')
    call('profile-train','--plan',labels1,'--plan',copy_file,'--out',profile,ok=False)
    assert not profile.exists(), 'Renaming/copying PCM cannot cross the independent validation boundary'
    source2=root/'independent.wav';sf.write(source2,samples*.8,rate,subtype='FLOAT')
    fullplan['source']=str(source2);fullplan['source_relative']='';fullplan['source_sha256']=sha(source2)
    labels2=root/'labels2.json';labels2.write_text(json.dumps(fullplan),encoding='utf-8')
    call('profile-train','--plan',labels1,'--plan',labels2,'--out',profile)
    assert profile.exists()
    snapshot=sha(profile)
    call('profile-train','--plan',labels1,'--out',profile,ok=False)
    assert sha(profile)==snapshot, 'Rejected training must preserve the valid profile'
    filtered=root/'filtered.json'
    proposals=json.loads(labels1.read_text(encoding='utf-8'))
    for e in proposals['events']: e.update(training_eligible=False,decision_source='automatic',enabled=True,approved=False,learning_label='')
    proposals_file=root/'proposals.json';proposals_file.write_text(json.dumps(proposals),encoding='utf-8')
    call('profile-filter','--plan',proposals_file,'--profile',profile,'--out',filtered)
    assert any(e.get('reason')=='personal_profile' and not e['enabled'] for e in json.loads(filtered.read_text(encoding='utf-8-sig'))['events'])
    # Independent click profile, trained through the actual worker, reusable by
    # both hosts. Test labels stay in this disposable synthetic test directory.
    click_plans=[]
    for number,src in enumerate((source,source2)):
        spec=json.loads(labels1.read_text());spec.update(source=str(src),source_relative='',source_sha256=sha(src))
        for i,e in enumerate(spec['events']):
            e.update(kind=1,end=e['start']+96,learning_label='mouth_click_confirmed' if i%2==0 else 'vocal_transient')
        path=root/f'click_truth_{number}.json';path.write_text(json.dumps(spec));click_plans.append(path)
    click_profile=root/'personal_click_profile.ini';folder=root/'click_training';folder.mkdir()
    job=run_job(folder,dict(mode='profile-train',kind='click',exe=str(exe),plans=list(map(str,click_plans)),output=str(click_profile)))
    assert job['status']=='complete',job
    assert 'schema=3' in click_profile.read_text() and 'category=click' in click_profile.read_text()
    wrong=root/'wrong_category.json'
    call('profile-filter','--kind','breath','--plan',proposals_file,'--profile',click_profile,'--out',wrong,ok=False)
    assert not wrong.exists()
    spec=json.loads(click_plans[0].read_text())
    for e in spec['events']:e.update(decision_source='automatic',training_eligible=False,learning_label='',enabled=True,approved=True)
    breath_event=dict(spec['events'][1],kind=0);spec['events'].append(breath_event)
    # Editorial decisions and training labels are never changed by a filter.
    user_event=dict(spec['events'][1],decision_source='user',training_eligible=False);spec['events'].append(user_event)
    path=root/'click_proposals.json';path.write_text(json.dumps(spec));output=root/'click_filtered.json'
    call('profile-filter','--kind','click','--plan',path,'--profile',click_profile,'--out',output)
    actual=json.loads(output.read_text())['events']
    assert any(e.get('reason')=='personal_profile' and not e['enabled'] for e in actual[:6])
    assert actual[-2]['enabled'] and actual[-1]['enabled'],'click profile changed breath or editorial decision'
    preserved=sha(click_profile)
    call('profile-train','--kind','click','--plan',labels1,'--out',click_profile,ok=False)
    assert sha(click_profile)==preserved,'failed click training overwrote the valid profile'
print('REAPER BRIDGE OK: real native WAV/MP3/FLAC crop, four edit modes, worker argv, cancellation and source conflict')
