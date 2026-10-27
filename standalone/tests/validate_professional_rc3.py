# SPDX-License-Identifier: MPL-2.0
"""Opt-in full-recording validation on private copies and sparse human point markers.

No training, threshold tuning, original writes or claims of exhaustive precision.
"""
import argparse
from collections import Counter
import csv
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import numpy as np
import soundfile as sf


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def seconds(text):
    h, m, s = map(float, text.split(':'))
    return 3600*h+60*m+s


def reviewed_sessions(cli, work, sessions):
    results=[]
    for number,session_path in enumerate(sessions,1):
        session=json.loads(session_path.read_text(encoding='utf-8-sig'))
        source=session_path.parent/Path(session['source']).name
        assert sha(source)==session['source_sha256'],'Review/source SHA mismatch'
        source_hash,session_hash=sha(source),sha(session_path)
        labels=[r for r in session['regions'] if r.get('training_eligible') and r.get('decision_source')=='user'
                and r.get('learning_label') in ('false_positive','click_false_positive','missed_breath','missed_click')]
        negative=[]
        for e in sorted((r for r in labels if r['learning_label'] in ('false_positive','click_false_positive')),key=lambda e:(e['kind'],e['start'])):
            if negative and negative[-1]['kind']==e['kind'] and e['start']<negative[-1]['end']:
                negative[-1]['end']=max(negative[-1]['end'],e['end'])
            else:negative.append(dict(kind=e['kind'],start=e['start'],end=e['end']))
        folder=work/f'review_{number:02d}';folder.mkdir()
        def call(*args):
            r=subprocess.run([str(cli),*map(str,args)],capture_output=True,timeout=180)
            if r.returncode:raise RuntimeError(r.stderr.decode('utf-8',errors='replace'))
        rows=[]
        for detector in ('dsp','fusion'):
            path=folder/(detector+'.json');call('plan',source,'--out',path,'--detector',detector,'--breaths',1,'--clicks',1)
            plan=json.loads(path.read_text());out=folder/detector
            call('process',source,'--out-dir',out,'--detector',detector,'--breaths',1,'--clicks',1)
            report=json.loads((out/(source.stem+'_breathclick.wav.report.json')).read_text())
            intersects=lambda e,n:e.get('kind',1)==n['kind'] and e['start']<n['end'] and e['end']>n['start']
            hits=[dict(**n,proposals=[e for e in plan['events'] if intersects(e,n)],
                       automatic_click_repairs=[e for e in report['click_decisions'] if e['applied'] and intersects(e,n)]) for n in negative]
            missed=[dict(kind=e['kind'],start=e['start'],end=e['end'],found=any(intersects(c,e) for c in plan['events']))
                    for e in labels if e['learning_label'] in ('missed_breath','missed_click')]
            rows.append(dict(detector=detector,negative_clusters=len(negative),
                             negative_clusters_proposed=sum(bool(n['proposals']) for n in hits),
                             negative_clusters_repaired_automatically=sum(bool(n['automatic_click_repairs']) for n in hits),
                             negative_details=hits,previous_missed_events=missed))
        assert sha(source)==source_hash and sha(session_path)==session_hash
        result=dict(source=str(source),source_sha256=source_hash,review_sha256=session_hash,
                    explicit_stored_labels=dict(Counter(r['learning_label'] for r in labels)),detectors=rows)
        results.append(result)
        print(json.dumps(dict(review=session_path.name,labels=result['explicit_stored_labels'],
                              metrics=[{k:r[k] for k in ('detector','negative_clusters','negative_clusters_proposed','negative_clusters_repaired_automatically')} for r in rows])),flush=True)
    return results


def write_listening_report(work, report):
    panels=[]
    for number,case in enumerate(report['cases']):
        rows=[]
        for point in case['points']:
            tolerance=.05 if point['kind']==1 else .25
            if point['kind']==0 and point['distance_s'] is not None and point['distance_s']<=tolerance:
                continue
            distance=point['distance_s']
            verdict='proposta vicina' if distance is not None and distance<=tolerance else 'nessuna proposta vicina'
            rows.append('<tr><td>'+html.escape(point['label'])+'</td><td>'+f'{point["time"]:.3f} s'+'</td><td>'+verdict+'</td><td><button data-player="p'+str(number)+'" data-time="'+str(point['time'])+'">Ascolta il punto</button></td></tr>')
        source=case['case']+'/original.wav'
        panels.append('<section><h2>'+html.escape(case['case'])+'</h2><p>Registrazione intera: '+f'{case["seconds"]:.1f} s'+'.</p><audio controls id="p'+str(number)+'" src="'+source+'"></audio><table><tr><th>Marker manuale</th><th>Tempo</th><th>Riscontro</th><th></th></tr>'+''.join(rows)+'</table></section>')
    for number,review in enumerate(report.get('stored_reviews',[])):
        source=Path(review['source']);rate=sf.info(source).samplerate
        url=os.path.relpath(source,work).replace('\\','/')
        rows=[]
        for n in review['detectors'][-1]['negative_details']:
            if n['proposals']:
                rows.append('<tr><td>'+f'{n["start"]/rate:.3f}–{n["end"]/rate:.3f} s'+'</td><td>Falso positivo registrato; nuova proposta da rivedere</td><td><button data-player="n'+str(number)+'" data-time="'+str(n['start']/rate)+'">Ascolta</button></td></tr>')
        automatic=review['detectors'][-1]['negative_clusters_repaired_automatically']
        panels.append('<section><h2>Etichette preesistenti: '+html.escape(source.stem)+'</h2><audio controls id="n'+str(number)+'" src="'+html.escape(url,quote=True)+'"></audio><table>'+''.join(rows)+'</table><p>Gli intervalli sovrapposti sono contati una volta. Gruppi negativi con riparazioni click automatiche: '+str(automatic)+'.</p></section>')
    page='''<!doctype html><html lang="it"><meta charset="utf-8"><title>NL Breath Click Studio — collaudo rilevamento</title>
<style>body{font:17px system-ui;max-width:1100px;margin:35px auto;padding:15px;color:#182333;background:#f5f7fa}section{background:white;padding:20px;margin:20px 0}audio{width:100%}td,th{padding:9px;text-align:left}button{padding:8px}</style>
<h1>Controllo del rilevamento sulle registrazioni intere</h1><p>Solo copie locali; nessun audio viene inviato online. Qui trovi tutti i punti click annotati e i respiri senza una proposta vicina. Il confronto cieco di Auto rimane separato e invariato.</p><p>Riferimenti: marker manuali puntuali, non un'annotazione esaustiva. Tolleranze: 50 ms per i click, 250 ms per i respiri. Una proposta aggiuntiva non e' automaticamente un falso positivo; una distanza ampia segnala un punto da verificare. Il file JSON contiene anche le altre tolleranze.</p>'''
    page+=''.join(panels)
    page+='''<script>document.querySelectorAll('button[data-player]').forEach(b=>b.onclick=()=>{document.querySelectorAll('audio').forEach(a=>a.pause());const a=document.getElementById(b.dataset.player),t=Number(b.dataset.time);a.currentTime=Math.max(0,t-.75);a.ontimeupdate=()=>{if(a.currentTime>t+1.25)a.pause()};a.play()});</script></html>'''
    (work/'CONTROLLO_RILEVAMENTO.html').write_text(page,encoding='utf-8')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cli', type=Path, required=True)
    p.add_argument('--recordings', type=Path)
    p.add_argument('--annotations', type=Path)
    p.add_argument('--case', action='append', default=[])
    p.add_argument('--review-session', type=Path, action='append', default=[])
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--resume-incomplete', action='store_true')
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[2]
    if not a.case and not a.review_session or a.case and not (a.recordings and a.annotations):
        raise SystemExit('Provide recording cases with annotations, or stored review sessions')
    if any(not p.resolve().is_relative_to(repo/'build') for p in a.review_session):
        raise SystemExit('Use existing build-local review copies')
    work = a.work.resolve()
    if (not work.is_relative_to(repo/'build') or (work/'result.json').exists() or
            work.exists() and not a.resume_incomplete):
        raise SystemExit('Use a new dedicated output directory under build/')
    if any(Path(c).name != c for c in a.case):
        raise SystemExit('Case names must be plain filename stems')
    work.mkdir(parents=True,exist_ok=True)
    cli = a.cli.resolve()

    def call(*args):
        r = subprocess.run([str(cli),*map(str,args)], capture_output=True, timeout=180)
        if r.returncode:
            raise RuntimeError(r.stderr.decode('utf-8', errors='replace'))
        return r.stdout

    caps = json.loads(call('capabilities'))
    results = []
    for case in a.case:
        folder = work/case
        folder.mkdir(exist_ok=True)
        original = a.recordings/(case+'.wav')
        annotation = a.annotations/(case+'.csv')
        protected = {p:sha(p) for p in (original,annotation)}
        source = folder/'original.wav'
        if source.exists():
            assert sha(source)==protected[original],'Existing input copy differs; preserved'
        else:
            shutil.copyfile(original,source)
        rows = list(csv.DictReader(annotation.read_text(encoding='utf-8-sig').splitlines()))
        points = [dict(label=r['Name'],time=seconds(r['Start']),
                       kind=1 if 'click' in r['Name'].casefold() else 0)
                  for r in rows if 'fiato' in r['Name'].casefold() or 'click' in r['Name'].casefold()]
        plans, elapsed = {}, {}
        for detector in ('dsp','fusion'):
            for mode, breath, click in (('breaths',1,0),('clicks',0,1),('both',1,1)):
                path = folder/(detector+'_'+mode+'.json')
                start = time.perf_counter()
                call('plan',source,'--out',path,'--detector',detector,'--breaths',breath,'--clicks',click)
                elapsed[detector+'_'+mode] = time.perf_counter()-start
                plan = json.loads(path.read_text(encoding='utf-8-sig'))
                assert all(e['kind'] in ([0] if mode=='breaths' else [1] if mode=='clicks' else [0,1]) for e in plan['events'])
                if mode=='clicks':
                    assert plan['ai_inference_frames']==0
                if detector=='fusion' and breath:
                    assert plan['ai_inference_frames']>0
                plans[detector+'_'+mode] = plan
            both = plans[detector+'_both']['events']
            for mode, kind in (('breaths',0),('clicks',1)):
                # Compare complete events, including scores/classification, not just counts.
                assert plans[detector+'_'+mode]['events']==[e for e in both if e['kind']==kind],(case,detector,mode)
        assert plans['dsp_clicks']['events']==plans['fusion_clicks']['events']
        plan = plans['fusion_both']
        sr = plan['sample_rate']
        measured = []
        for point in points:
            candidates = [e for e in plan['events'] if e['kind']==point['kind']]
            distance = lambda e:max(e['start']/sr-point['time'],point['time']-e['end']/sr,0.)
            nearest = min(candidates,key=distance) if candidates else None
            measured.append(dict(**point,distance_s=distance(nearest) if nearest else None,
                                 nearest=nearest))
        matching = {}
        for kind, name, tolerances in ((0,'breaths',(.1,.25,.5)),(1,'clicks',(.02,.05,.1))):
            relevant = [r for r in measured if r['kind']==kind]
            matching[name] = {str(t):dict(annotated=len(relevant),matched=sum(r['distance_s'] is not None and r['distance_s']<=t for r in relevant),
                                        missed=sum(r['distance_s'] is None or r['distance_s']>t for r in relevant)) for t in tolerances}
        # Exercise actual unattended policy over the WHOLE recording, not forced repairs.
        output = folder/'automatic'
        call('process',source,'--out-dir',output,'--detector','fusion','--breaths',1,'--clicks',1)
        before, rate = sf.read(source,dtype='float32',always_2d=True)
        after, ar = sf.read(output/'original_breathclick.wav',dtype='float32',always_2d=True)
        assert rate==ar and before.shape==after.shape
        report = json.loads((output/'original_breathclick.wav.report.json').read_text(encoding='utf-8-sig'))
        mask = np.zeros(len(before), dtype=bool)
        for event in plan['events']:
            if event['kind']==0 and event['enabled']:
                mask[event['start']:event['end']] = True
        for op in report['click_decisions']:
            if op['applied']:
                mask[op['start']:op['end']] = True
        assert np.array_equal(before[~mask],after[~mask]),'Audio outside applied intervals changed'
        assert all(sha(path)==h for path,h in protected.items()) and sha(source)==protected[original]
        unverified = sum(not any(p['kind']==e['kind'] and max(e['start']/sr-p['time'],p['time']-e['end']/sr,0.)<=(.25 if e['kind']==0 else .05)
                                 for p in points) for e in plan['events'])
        result = dict(case=case,source_sha256=protected[original],annotation_sha256=protected[annotation],
                      seconds=len(before)/rate,channels=before.shape[1],sample_rate=rate,
                      candidates=dict(Counter(str(e['kind']) for e in plan['events'])),
                      classification=dict(Counter(e['classification'] for e in plan['events'])),
                      human_point_matching=matching,points=measured,
                      proposals_without_point_reference=unverified,
                      automatic_applied=report['processed_events'],
                      automatic_declined=sum(c['approved'] and not c['applied'] for c in report['click_decisions']),
                      unchanged_audio_outside_applied=True,originals_unchanged=True,analysis_seconds=elapsed)
        results.append(result)
        print(json.dumps({k:result[k] for k in ('case','seconds','candidates','human_point_matching','proposals_without_point_reference')}),flush=True)
    reviews=reviewed_sessions(cli,work,a.review_session)
    report = dict(status='passed',functional_status='passed',quality_status='not_certified',capabilities=caps,
                  scope='Sparse human points; unmatched proposals are UNVERIFIED, not measured false positives. '
                        'Point tolerance is not an interval accuracy score. No training or perceptual claim.',
                  cases=results,stored_reviews=reviews)
    (work/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    write_listening_report(work,report)


if __name__=='__main__':
    main()
