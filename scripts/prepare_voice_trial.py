# SPDX-License-Identifier: MPL-2.0
"""Local listening trial from read-only user recordings; never training data.

Only short WAV copies, plans and reports are written, strictly under build/.
Numerical invariants do not establish perceptual quality or ground truth.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, nargs='+', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=12)
    parser.add_argument('--compare-methods', action='store_true',
                        help='Also render forced alternatives on ALL proposals, including uncertain events; listening only')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work, cli = args.work.resolve(), args.cli.resolve()
    if not work.is_relative_to(repo / 'build') or work == repo / 'build':
        raise SystemExit('Trial output must be a dedicated directory under build/')
    if work.exists() and any(work.iterdir()):
        raise SystemExit('Use an empty trial directory; existing work is preserved')
    if not 1 <= args.seconds <= 30:
        raise SystemExit('Clip duration must be 1..30 seconds')
    inputs = [p.resolve(strict=True) for p in args.input]
    if len(set(inputs)) != len(inputs):
        raise SystemExit('Duplicate input recording')
    if any(p.is_relative_to(work) for p in inputs):
        raise SystemExit('Input must not be inside the output directory')
    work.mkdir(parents=True, exist_ok=True)

    def call(*values):
        r = subprocess.run([str(cli), *map(str, values)], capture_output=True,
                           encoding='utf-8', timeout=180)
        if r.returncode:
            raise RuntimeError(r.stderr)
        return r.stdout

    report = dict(scope='Private local copies, no human labels or accuracy/naturalness claims',
                  capabilities=json.loads(call('capabilities')), sources=[], cases=[])
    panels = []
    for number, source in enumerate(inputs, 1):
        digest = sha(source)
        info = sf.info(source)
        count = round(args.seconds * info.samplerate)
        if info.frames <= count * 2:
            excerpts = [('whole', 0, info.frames)]
        else:
            middle = min((info.frames-count)//2, info.frames-2*count)
            excerpts = [('voice', middle, count), ('tail', info.frames-count, count)]
        report['sources'].append(dict(path=str(source), sha256=digest,
                                      frames=info.frames, sample_rate=info.samplerate))
        for label, first, length in excerpts:
            name = f'case_{number:02d}_{label}'
            folder = work / name
            folder.mkdir()
            with sf.SoundFile(source) as audio:
                audio.seek(first)
                data = audio.read(length, dtype='float32', always_2d=True)
            original = folder / 'original.wav'
            sf.write(original, data, info.samplerate, subtype='FLOAT')
            original_sha = sha(original)
            plan_path = folder / 'detected.json'
            call('plan', original, '--out', plan_path, '--breaths', 0, '--clicks', 1, '--detector', 'dsp')
            plan = json.loads(plan_path.read_text(encoding='utf-8-sig'))
            case = dict(name=name, source_index=number, source_first_frame=first,
                        frames=len(data), sample_rate=info.samplerate,
                        candidate_count=len(plan['events']), methods={})
            buttons = [('Originale', f'{name}/original.wav')]
            methods = ['automatic'] + (['linear', 'predictive'] if args.compare_methods else [])
            for method in methods:
                output = folder / method
                output.mkdir()
                spec = json.loads(json.dumps(plan))
                if method == 'automatic':
                    # `plan` returns unreviewed proposals. Reproduce automatic
                    # inclusion, then independently compare the real process path
                    # below so unapproved events cannot make a vacuous no-op test.
                    for event in spec['events']:
                        event.update(approved=event['classification'] in ('digital_click', 'saliva_click'),
                                     decision_source='automatic', training_eligible=False)
                else:
                    for event in spec['events']:
                        event.update(enabled=True, approved=True, repair=True,
                                     repair_method=method, mode='attenuate',
                                     decision_source='review_harness', training_eligible=False)
                path = output / 'plan.json'
                path.write_text(json.dumps(spec), encoding='utf-8')
                call('edit', original, '--plan', path, '--out-dir', output)
                call('difference', original, '--plan', path, '--out-dir', output)
                processed, rate = sf.read(output/'original_edit.wav', dtype='float32', always_2d=True)
                residual, residual_rate = sf.read(output/'original_difference.wav', dtype='float32', always_2d=True)
                assert rate == residual_rate == info.samplerate and processed.shape == residual.shape == data.shape
                assert np.max(np.abs(residual-(data-processed))) < 1e-7
                if method == 'automatic':
                    exported = output/'batch'
                    exported.mkdir()
                    call('process', original, '--out-dir', exported, '--breaths', 0, '--clicks', 1, '--detector', 'dsp')
                    batch, batch_rate = sf.read(exported/'original_breathclick.wav', dtype='float32', always_2d=True)
                    assert batch_rate == rate and np.array_equal(batch, processed), 'Automatic preview/batch mismatch'
                changed = np.any(processed != data, axis=1)
                allowed = np.zeros(len(data), dtype=bool)
                for event in spec['events']:
                    allowed[max(0,event['start']):min(len(data),event['end'])] = True
                assert not np.any(changed & ~allowed), 'Audio outside proposed events changed'
                render = json.loads((output/'original_edit.wav.edit.json').read_text(encoding='utf-8-sig'))
                case['methods'][method] = dict(changed_frames=int(changed.sum()),
                    applied=render['applied_events'], declined=render['declined_events'],
                    rejected=render['rejected_events'])
                title = {'automatic':'Auto', 'linear':'Lineare (forzata)', 'predictive':'Predittiva (forzata)'}[method]
                buttons.extend([(title, f'{name}/{method}/original_edit.wav'),
                                (title+' — Solo rimosso', f'{name}/{method}/original_difference.wav')])
            assert sha(original) == original_sha
            report['cases'].append(case)
            audio_id = 'audio_'+name
            links = ' '.join('<button data-audio="'+audio_id+'" data-src="'+html.escape(url, quote=True)+'">'+html.escape(title)+'</button>' for title,url in buttons)
            panels.append(f'<section><h2>{name}</h2><p>{len(plan["events"])} proposte da rivedere; nessuna classificazione umana.</p><audio id="{audio_id}" controls preload="none" src="{name}/original.wav"></audio><p>{links}</p><p class="choice">Originale</p></section>')
        assert sha(source) == digest, 'Original recording changed during trial'
    page = '''<!doctype html><html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>NL Breath Click Studio — ascolto locale RC3</title>
<style>body{font:17px system-ui;max-width:1000px;margin:40px auto;padding:0 20px;background:#f6f7fa;color:#182333}section{background:white;padding:20px;margin:24px 0;border:1px solid #ccd3dd;border-radius:8px}button{padding:10px;margin:4px;cursor:pointer}audio{width:100%}h2{font-size:20px}.choice{font-weight:bold}</style>
<h1>NL Breath Click Studio — ascolto locale RC3</h1><p>Copie brevi delle tue registrazioni. Questa pagina non carica audio online.</p>
<p>Alterna Originale, Auto e Solo rimosso mantenendo il punto di ascolto. Ascolta anche le parole vicine. Le versioni forzate provano tutte le proposte, anche quelle incerte: possono peggiorare la voce e non sono consigli automatici.</p>
<p>I finali possono contenere mouse o tastiera: non sono automaticamente schiocchi di saliva. Le proposte non sono annotazioni verificate.</p>'''
    page += '\n'.join(panels)
    page += '''<script>document.querySelectorAll('button[data-src]').forEach(b=>b.onclick=()=>{const a=document.getElementById(b.dataset.audio),t=a.currentTime,playing=!a.paused;a.pause();a.onloadedmetadata=()=>{a.currentTime=Math.min(t,Math.max(0,a.duration-.01));if(playing)a.play();};a.src=b.dataset.src;b.closest('section').querySelector('.choice').textContent=b.textContent;});</script></html>'''
    (work/'ASCOLTA.html').write_text(page, encoding='utf-8')
    (work/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(status='passed', recordings=len(inputs), cases=len(report['cases']),
                         original_hashes_unchanged=True, duration_and_residual_verified=True,
                         listening_page=str(work/'ASCOLTA.html'), limits=report['scope'])))


if __name__ == '__main__':
    main()
