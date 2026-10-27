# SPDX-License-Identifier: MPL-2.0
"""Matched-region blind listening study. Private audio stays under build/.

Approval is identical across Auto/Linear/Burg. Detection/approval policy is
measured separately; neither generated plans nor listening votes train AI.
"""
import argparse
import collections
import hashlib
import html
import json
import os
from pathlib import Path
import random
import subprocess
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_voice_trial import sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--trial', type=Path, required=True)
    p.add_argument('--cli', type=Path, required=True)
    p.add_argument('--baseline-cli', type=Path, required=True)
    p.add_argument('--holdout', type=Path, nargs='+', default=[])
    p.add_argument('--reuse-study', type=Path,
                   help='Revisit exact clips from an existing voted study as development data')
    p.add_argument('--case-id', action='append', default=[], help='Select revisited clip IDs')
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work, trial = a.work.resolve(), a.trial.resolve()
    if not work.is_relative_to(repo/'build') or work == repo/'build':
        raise SystemExit('Output must be a dedicated directory under build/')
    if work.exists() and any(work.iterdir()):
        raise SystemExit('Existing listening study preserved; choose an empty output directory')
    old = json.loads((trial/'report.json').read_text(encoding='utf-8'))
    if a.reuse_study:
        revisited=json.loads((a.reuse_study/'report.json').read_text(encoding='utf-8'))
        used=set().union(*map(set,revisited['recording_groups'].values()))
        if set(a.case_id)-{c['id'] for c in revisited['cases']}:
            raise SystemExit('Unknown revisited case ID')
    else:
        if a.case_id:raise SystemExit('--case-id requires --reuse-study')
        used = {s['sha256'] for s in old['sources']}
    if not a.holdout and not a.reuse_study:
        raise SystemExit('Provide held-out recordings or an existing study to revisit')
    # Reserve recording groups before examining any new detector result.
    holdout = [(x.resolve(strict=True), sha(x)) for x in a.holdout]
    if len({h for _,h in holdout}) != len(holdout) or any(h in used for _,h in holdout):
        raise SystemExit('Held-out recordings must differ from development recordings')
    work.mkdir(parents=True)
    cli, baseline = a.cli.resolve(), a.baseline_cli.resolve()

    def call(executable, *args):
        result = subprocess.run([str(executable), *map(str,args)], capture_output=True,
                                encoding='utf-8', timeout=180)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return result.stdout

    capabilities = json.loads(call(cli,'capabilities'))
    assert capabilities.get('click_auto_selector',0) >= 2
    study_id = os.urandom(12).hex()
    report = dict(schema=1, study_id=study_id, capabilities=capabilities,
        selector_sha256=sha(repo/'core/src/dsp/click_repair.cpp'),
        source_base_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo).decode().strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=repo)),
        scope='Matched proposals, not detection ground truth or perceptual proof; no training',
        cases=[], recording_groups={'development':sorted(used),'holdout':[h for _,h in holdout]})
    cases = []
    if a.reuse_study:
        for index,c in enumerate(revisited['cases'],1):
            if not a.case_id or c['id'] in a.case_id:
                cases.append((Path(c['source']),a.reuse_study/f'clip_{index:02d}'/'A'/'plan.json',
                              'development',c['id']))
    else:
        for c in old['cases']:
            cases.append((trial/c['name']/'original.wav', trial/c['name']/'detected.json', 'development', c['name']))
    for number,(source,digest) in enumerate(holdout,1):
        info = sf.info(source)
        frames = min(info.frames, info.samplerate*8)
        first = (info.frames-frames)//2
        folder = work/f'holdout_{number:02d}'
        folder.mkdir()
        with sf.SoundFile(source) as audio:
            audio.seek(first);data=audio.read(frames,dtype='float32',always_2d=True)
        copy = folder/'original.wav';sf.write(copy,data,info.samplerate,subtype='FLOAT')
        plan = folder/'detected.json'
        call(cli,'plan',copy,'--out',plan,'--breaths',0,'--clicks',1,'--detector','dsp')
        name=('new_holdout_' if a.reuse_study else 'holdout_')+f'{number:02d}'
        cases.append((copy,plan,'holdout',name))
        assert sha(source)==digest
    panels=[];key={}
    for number,(source,detected,group,name) in enumerate(cases,1):
        folder=work/f'clip_{number:02d}';folder.mkdir()
        x,rate=sf.read(source,dtype='float32',always_2d=True)
        digest=sha(source);plan=json.loads(detected.read_text(encoding='utf-8-sig'))
        for e in plan['events']:
            e.update(approved=True,enabled=True,repair=True,mode='attenuate',
                     decision_source='review_harness',training_eligible=False)
        mask=np.zeros(len(x),dtype=bool)
        for e in plan['events']:mask[max(0,e['start']):min(len(x),e['end'])]=True
        methods=['auto','linear','predictive'];random.SystemRandom().shuffle(methods)
        key[name]=dict(zip('ABC',methods))
        record=dict(id=name,group=group,source=str(source),source_sha256=digest,
                    events=[(e['start'],e['end']) for e in plan['events']],variants={})
        urls={};removed={}
        for letter,method in zip('ABC',methods):
            output=folder/letter;output.mkdir()
            for e in plan['events']:e['repair_method']=method
            path=output/'plan.json';path.write_text(json.dumps(plan),encoding='utf-8')
            call(cli,'edit',source,'--plan',path,'--out-dir',output)
            call(cli,'difference',source,'--plan',path,'--out-dir',output)
            y,yrate=sf.read(output/'original_edit.wav',dtype='float32',always_2d=True)
            residual,rrate=sf.read(output/'original_difference.wav',dtype='float32',always_2d=True)
            assert yrate==rrate==rate and y.shape==residual.shape==x.shape
            assert not np.any(y[~mask]!=x[~mask]),'Repair expanded outside the common regions'
            assert np.max(np.abs(residual-(x-y)))<1e-7
            r=json.loads((output/'original_edit.wav.edit.json').read_text(encoding='utf-8-sig'))
            if method in ('linear','predictive'):
                assert all(op['declined'] or op['click_strategy']['selected']==method for op in r['operations'])
            record['variants'][letter]=dict(method=method,applied=r['applied_events'],declined=r['declined_events'],
                selected=dict(collections.Counter(op['click_strategy']['selected'] for op in r['operations'])),
                changed_frames=int(np.any(y!=x,axis=1).sum()),peak=float(np.max(np.abs(y))),rms=float(np.sqrt(np.mean(y*y))))
            urls[letter]=os.path.relpath(output/'original_edit.wav',work).replace('\\','/')
            removed[letter]=os.path.relpath(output/'original_difference.wav',work).replace('\\','/')
        # Old Auto receives the SAME approved intervals, not its old smaller
        # automatic-detection subset. Only the selector differs in this check.
        previous=folder/'previous';previous.mkdir()
        for e in plan['events']:e['repair_method']='auto'
        path=previous/'plan.json';path.write_text(json.dumps(plan),encoding='utf-8')
        call(baseline,'edit',source,'--plan',path,'--out-dir',previous)
        r=json.loads((previous/'original_edit.wav.edit.json').read_text(encoding='utf-8-sig'))
        record['previous_auto']=dict(applied=r['applied_events'],declined=r['declined_events'])
        assert sha(source)==digest
        report['cases'].append(record)
        original=os.path.relpath(source,work).replace('\\','/')
        audio_id=f'audio_{number}'
        def button(label,url):
            return '<button data-audio="'+audio_id+'" data-src="'+html.escape(url,quote=True)+'">'+label+'</button>'
        buttons=button('Originale',original)
        buttons+=' '.join(button(k,urls[k])+button(k+' · Solo rimosso',removed[k]) for k in 'ABC')
        preference='<select data-field="preferred"><option value="">Non ancora scelto</option>'+''.join('<option>'+k+'</option>' for k in ('A','B','C','Originale / nessuna','Parita'))+'</select>'
        rows=''
        for letter in 'ABC':
            cells=''
            for metric in ('rimozione','voce','artefatti'):
                cells+='<td><select data-field="'+letter+'_'+metric+'"><option value="">—</option>'+''.join('<option>'+str(i)+'</option>' for i in range(1,6))+'</select></td>'
            rows+='<tr><th>'+letter+'</th>'+cells+'</tr>'
        panels.append(f'<section data-case="{name}"><h2>Estratto {number}</h2><audio id="{audio_id}" controls preload="none" src="{html.escape(original,quote=True)}"></audio><p>{buttons}</p><p class="choice">Originale</p><label>Versione preferita: {preference}</label><p><textarea data-field="note" placeholder="Click rimasto, voce alterata, punto da rivedere…"></textarea></p><details><summary>Valutazione facoltativa delle tre versioni</summary><p>Da 1 a 5: rimozione del click e fedelta della voce (5 = migliore); artefatti (5 = piu evidenti).</p><table><tr><th></th><th>Rimozione</th><th>Voce</th><th>Artefatti</th></tr>{rows}</table></details></section>')
    page='''<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>NL Breath Click Studio — confronto cieco</title>
<style>body{font:17px system-ui;max-width:1000px;margin:40px auto;padding:0 20px;background:#f5f7fa;color:#182333}section{padding:20px;background:white;border:1px solid #ccd3dd;border-radius:8px;margin:24px 0}audio,textarea{width:100%}textarea{min-height:60px}button,select{padding:9px;margin:4px;cursor:pointer}.choice{font-weight:bold}th,td{padding:7px}h2{font-size:21px}</style>
<h1>Quale versione conserva meglio la tua voce?</h1><p>Le versioni A/B/C usano le stesse regioni, gli stessi confini e lo stesso volume. Il loro ordine cambia da un estratto all'altro. I metodi rimangono nascosti durante l'ascolto.</p><p>Alterna le versioni nello stesso punto. Ascolta anche Solo rimosso: consonanti o pezzi di voce sono un motivo per rivedere l'intervento. Puoi preferire l'originale o indicare una parita. Le regioni sono proposte da valutare, non click gia confermati.</p><p>Le preferenze restano in questo browser. Usa Esporta risposte per conservarle in un file separato; non diventano dati di addestramento. Questa pagina non invia audio o risposte online.</p><button id="export">Esporta risposte</button><p id="storage"></p>'''
    page+='\n'.join(panels)
    page+='''<script>
const study=STUDY_ID,storageKey='nlbc-listening-'+study;let answers={};try{answers=JSON.parse(localStorage.getItem(storageKey)||'{}');}catch(e){document.getElementById('storage').textContent='Salvataggio nel browser non disponibile: usa Esporta risposte prima di chiudere.';}
document.querySelectorAll('section').forEach(s=>s.querySelectorAll('[data-field]').forEach(f=>{f.value=(answers[s.dataset.case]||{})[f.dataset.field]||'';f.addEventListener('input',()=>{(answers[s.dataset.case]??={})[f.dataset.field]=f.value;try{localStorage.setItem(storageKey,JSON.stringify(answers));}catch(e){document.getElementById('storage').textContent='Usa Esporta risposte per conservare le tue preferenze.';}});}));
document.querySelectorAll('button[data-src]').forEach(b=>b.onclick=()=>{const a=document.getElementById(b.dataset.audio),t=a.currentTime,playing=!a.paused;a.pause();a.onloadedmetadata=()=>{a.currentTime=Math.min(t,Math.max(0,a.duration-.01));if(playing)a.play();};a.src=b.dataset.src;b.closest('section').querySelector('.choice').textContent=b.textContent;});
document.getElementById('export').onclick=()=>{const payload={schema:1,type:'nl.breathclick.listening-preferences',study_id:study,training_eligible:false,answers},a=document.createElement('a'),url=URL.createObjectURL(new Blob([JSON.stringify(payload,null,2)],{type:'application/json'}));a.href=url;a.download='Preferenze_ascolto_'+study+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
</script></html>'''.replace('STUDY_ID',json.dumps(study_id))
    (work/'ASCOLTA.html').write_text(page,encoding='utf-8')
    (work/'CHIAVE_PRIVATA.json').write_text(json.dumps(dict(study_id=study_id,mapping=key),indent=2),encoding='utf-8')
    (work/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    for source,digest in holdout:assert sha(source)==digest
    print(json.dumps(dict(status='passed',cases=len(cases),development=len(cases)-len(holdout),holdout=len(holdout),
                         equal_regions_and_levels=True,unchanged_sources=True,listening_page=str(work/'ASCOLTA.html'))))


if __name__=='__main__':
    main()
