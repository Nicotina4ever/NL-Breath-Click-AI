# SPDX-License-Identifier: MPL-2.0
"""Opt-in before/after review of a voted study. Private media stays under build.

Reuses exact intervals and approvals; no detector tuning, training or new votes.
"""
import argparse
from collections import Counter
import hashlib
import html
import json
import os
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--study', type=Path, required=True)
    p.add_argument('--cli', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--case', default='new_holdout_02')
    p.add_argument('--start', type=float, default=5.)
    p.add_argument('--end', type=float, default=6.)
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[1]
    study, work = a.study.resolve(), a.work.resolve()
    if (not study.is_relative_to(repo/'build') or not work.is_relative_to(repo/'build')
            or work == repo/'build' or work == study or work.is_relative_to(study)
            or work.exists() or not 0 <= a.start < a.end):
        raise SystemExit('Use existing build-local study and a NEW separate output directory')
    report = json.loads((study/'report.json').read_text(encoding='utf-8-sig'))
    key = json.loads((study/'CHIAVE_PRIVATA.json').read_text(encoding='utf-8-sig'))
    assert key['study_id'] == report['study_id']
    assert a.case in {c['id'] for c in report['cases']}
    protected = {path: sha(path) for path in study.rglob('*') if path.is_file()}
    work.mkdir(parents=True)
    results, panels = [], []
    method_names = {'auto': 'Auto', 'linear': 'Lineare', 'predictive': 'Burg'}

    def run(*args):
        r = subprocess.run([str(a.cli.resolve()), *map(str,args)], capture_output=True, timeout=180)
        if r.returncode:
            raise RuntimeError(r.stderr.decode('utf-8',errors='replace'))

    def url(path):
        return html.escape(os.path.relpath(path, work).replace('\\','/'),quote=True)

    for index, case in enumerate(report['cases'], 1):
        source = Path(case['source'])
        assert source.resolve().is_relative_to(repo/'build')
        assert sha(source) == case['source_sha256']
        original, rate = sf.read(source, dtype='float32', always_2d=True)
        folder = work/f'clip_{index:02d}'
        folder.mkdir()
        old_folder = study/f'clip_{index:02d}'
        plan = json.loads((old_folder/'A/plan.json').read_text(encoding='utf-8-sig'))
        assert [(e['start'],e['end']) for e in plan['events']] == [tuple(e) for e in case['events']]
        mask = np.zeros(len(original), dtype=bool)
        for event in plan['events']:
            mask[event['start']:event['end']] = True
        variants = {}
        controls = f'<button data-src="{url(source)}">Originale</button>'
        for method in ('auto','linear','predictive'):
            letter = next(l for l,m in key['mapping'][case['id']].items() if m == method)
            assert case['variants'][letter]['method'] == method
            previous = old_folder/letter/'original_edit.wav'
            old, old_rate = sf.read(previous,dtype='float32',always_2d=True)
            assert old_rate == rate and old.shape == original.shape
            for event in plan['events']:
                event['repair_method'] = method
            spec = folder/(method+'.json')
            spec.write_text(json.dumps(plan),encoding='utf-8')
            out = folder/method
            run('edit',source,'--plan',spec,'--out-dir',out)
            run('difference',source,'--plan',spec,'--out-dir',out)
            output = out/(source.stem+'_edit.wav')
            residual_path = out/(source.stem+'_difference.wav')
            y, yr = sf.read(output,dtype='float32',always_2d=True)
            residual, rr = sf.read(residual_path,dtype='float32',always_2d=True)
            assert rate == yr == rr and y.shape == residual.shape == original.shape
            assert np.array_equal(y[~mask], original[~mask])
            assert np.max(np.abs(residual-(original-y))) < 1e-7
            if method == 'auto':
                assert np.array_equal(y,old),'Auto changed despite manual-only correction'
            edited = json.loads((out/(source.stem+'_edit.wav.edit.json')).read_text())
            declined = [op for op in edited['operations'] if op['declined']]
            for op in declined:
                assert np.array_equal(y[op['start']:op['end']],original[op['start']:op['end']])
            point_results = []
            if case['id'] == a.case:
                for op in edited['operations']:
                    lo, hi = op['start'], op['end']
                    if lo >= a.end*rate or hi <= a.start*rate:
                        continue
                    rms = lambda x: float(np.sqrt(np.mean(x[lo:hi].astype('float64')**2)))
                    boundaries = lambda x: [float(np.max(np.abs(x[j]-x[j-1]))) for j in (lo,hi)]
                    point_results.append(dict(start=lo,end=hi,start_seconds=lo/rate,end_seconds=hi/rate,
                        old_rms=rms(old),new_rms=rms(y),original_rms=rms(original),
                        old_boundary_steps=boundaries(old),new_boundary_steps=boundaries(y),
                        original_boundary_steps=boundaries(original),declined=op['declined'],
                        diagnostics=op['click_strategy'],original_preserved=np.array_equal(y[lo:hi],original[lo:hi])))
            variants[method] = dict(original_letter=letter,applied=edited['applied_events'],
                declined=edited['declined_events'],decline_reasons=dict(Counter(op['click_strategy']['reason'] for op in declined)),
                changed_frames_from_previous=int(np.any(y!=old,axis=1).sum()),
                auto_pcm_unchanged=bool(np.array_equal(y,old)) if method=='auto' else None,
                outside_pcm_exact=True,residual_max_error=float(np.max(np.abs(residual-(original-y)))),
                window_operations=point_results)
            name = method_names[method]
            if method != 'auto':
                controls += f'<button data-src="{url(previous)}">{name} · prima</button>'
            controls += f'<button data-src="{url(output)}">{name}'+(' · invariato' if method=='auto' else ' · corretto')+'</button>'
            controls += f'<button data-src="{url(residual_path)}">{name} · solo rimosso</button>'
        assert sha(source) == case['source_sha256']
        results.append(dict(id=case['id'],scope='development after listening; not held-out validation',
                            source_sha256=case['source_sha256'],frames=len(original),sample_rate=rate,variants=variants))
        special = case['id'] == a.case
        seek = f'<button data-seek="{max(0,a.start-.2)}" data-stop="{a.end+.2}">Ascolta il passaggio segnalato</button>' if special else ''
        content = f'<h2>Estratto {index}'+(' · passaggio segnalato' if special else '')+f'</h2><audio controls preload="none" src="{url(source)}"></audio><p>{controls}</p><p class="choice">Originale</p>{seek}'
        panels.append((special,f'<section>{content}</section>'))
    assert all(sha(path)==expected for path,expected in protected.items())
    result = dict(status='passed',study_id=report['study_id'],reviewed_case=a.case,
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=repo)),
        repair_source_sha256=sha(repo/'core/src/dsp/click_repair.cpp'),
        auto_pcm_unchanged_all_cases=True,previous_study_and_votes_unchanged=len(protected),
        training_eligible=False,scope='Numerical safety checks; corrected voice quality still requires listening',cases=results)
    (work/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    page='''<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>NL Breath Click Studio — verifica della correzione</title><style>body{font:17px system-ui;max-width:1000px;margin:35px auto;padding:15px;color:#182333;background:#f5f7fa}section{background:white;padding:20px;margin:25px 0;border:1px solid #ccd3dd;border-radius:8px}audio{width:100%}button{padding:10px;margin:4px}.choice{font-weight:bold}</style><h1>Confronto della correzione</h1><p>Il primo pannello ripropone l'estratto 5. Ascolta il passaggio segnalato, poi alterna Originale, Auto e le versioni Lineare/Burg prima e dopo la correzione. La posizione d'ascolto viene conservata.</p><p>Auto è identico al confronto già votato. La correzione impedisce anche ai metodi manuali di sostituire un frammento di voce quando la ricostruzione è troppo rischiosa: in quel punto viene conservato l'originale, senza attenuazione aggiuntiva. Solo rimosso permette di controllare cosa cambia.</p><p>Questo è un confronto dichiarato prima/dopo, non un nuovo test cieco. Gli altri quattro estratti sono disponibili più sotto. Audio e preferenze precedenti restano intatti; nessun invio online.</p>'''
    page += ''.join(panel for special,panel in panels if special)
    page += '<details><summary>Gli altri quattro estratti</summary>'+''.join(panel for special,panel in panels if not special)+'</details>'
    page += '''<script>document.querySelectorAll('section').forEach(s=>{const a=s.querySelector('audio'),choice=s.querySelector('.choice');s.querySelectorAll('button[data-src]').forEach(b=>b.onclick=()=>{const t=a.currentTime,playing=!a.paused;a.pause();a.onloadedmetadata=()=>{a.currentTime=Math.min(t,Math.max(0,a.duration-.01));if(playing)a.play();};a.src=b.dataset.src;choice.textContent=b.textContent;});s.querySelectorAll('button[data-seek]').forEach(b=>b.onclick=()=>{document.querySelectorAll('audio').forEach(other=>{if(other!==a)other.pause()});a.currentTime=Number(b.dataset.seek);a.ontimeupdate=()=>{if(a.currentTime>Number(b.dataset.stop))a.pause()};a.play();});});</script></html>'''
    (work/'ASCOLTA_CORREZIONE.html').write_text(page,encoding='utf-8')
    print(json.dumps(dict(status='passed',cases=len(results),auto_pcm_unchanged=True,study_and_votes_unchanged=len(protected))))


if __name__ == '__main__':
    main()
