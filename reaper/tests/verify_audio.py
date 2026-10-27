# SPDX-License-Identifier: MPL-2.0
"""Measure the actual media files attached to takes by the real REAPER test."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import soundfile as sf

parser = argparse.ArgumentParser()
parser.add_argument('report', type=Path)
args = parser.parse_args()
result = json.loads(args.report.read_text(encoding='utf-8-sig'))
assert result['status'] == 'passed'
source = Path(result['source'])
raw, rate = sf.read(source, dtype='float32', always_2d=True)
if result.get('professional'):
    metrics=[]
    for entry in result['outputs']:
        original, sr = sf.read(entry['source'],dtype='float32',always_2d=True)
        edited, er = sf.read(entry['output'],dtype='float32',always_2d=True)
        assert sr==er and original.shape==edited.shape
        edit_report=json.loads(Path(entry['output']+'.edit.json').read_text(encoding='utf-8-sig'))
        mask=np.zeros(len(original),dtype=bool)
        for op in edit_report['operations']:
            if op['applied']:mask[op['start']:op['end']]=True
        assert np.array_equal(original[~mask],edited[~mask]),'Unselected take samples changed'
        residual_error=0.
        if entry.get('residual'):
            removed, rr=sf.read(entry['residual'],dtype='float32',always_2d=True)
            assert rr==sr and removed.shape==original.shape
            residual_error=float(np.max(np.abs(removed-(original-edited))))
            assert residual_error<1e-7
        metrics.append(dict(frames=len(original),outside_pcm_exact=True,residual_max_error=residual_error))
    report=dict(status='passed',metrics=metrics)
    args.report.with_name('professional_audio_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report));raise SystemExit(0)
if 'clean' in result:  # Actual sources attached by mouth_click.lua, not a CLI simulation.
    clean, _ = sf.read(result['clean'], dtype='float32', always_2d=True)
    edited, sr = sf.read(result['output'], dtype='float32', always_2d=True)
    residual, _ = sf.read(result['residual'], dtype='float32', always_2d=True)
    attenuated, _ = sf.read(result['attenuated'], dtype='float32', always_2d=True)
    assert sr == rate and raw.shape == clean.shape == edited.shape == residual.shape == attenuated.shape
    changed = np.any(raw != edited, axis=1)
    assert np.any(changed) and np.max(np.abs(edited-clean)) < .001
    assert np.max(np.abs(residual-(raw-edited))) < 1e-7
    # A predictive repair may reconstruct some requested samples EXACTLY.
    # Its actual PCM differences are not the full region to which gain applies.
    edit_report = json.loads(Path(str(result['output'])+'.edit.json').read_text(encoding='utf-8-sig'))
    region = np.zeros(len(raw), dtype=bool)
    for operation in edit_report['operations']:
        if operation['applied']:
            first, last = operation['start'], operation['end']
            assert 0 <= first < last <= len(raw)
            region[first:last] = True
    assert np.any(region) and np.all(~changed | region)
    assert np.max(np.abs(attenuated[region]-raw[region]*10**(-12/20))) < 1e-7
    assert np.array_equal(attenuated[~region],raw[~region])
    uncertain, _ = sf.read(result['declined_source'], dtype='float32', always_2d=True)
    preserved, _ = sf.read(result['declined_output'], dtype='float32', always_2d=True)
    assert np.array_equal(uncertain,preserved), 'Declined reconstruction changed actual take PCM'
    report = dict(status='passed', frames=len(raw), modified_frames=int(np.count_nonzero(changed)),
                  repair_region_frames=int(np.count_nonzero(region)),
                  residual_max_error=float(np.max(np.abs(residual-(raw-edited)))), declined_pcm_exact=True)
    args.report.with_name('click_audio_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report));raise SystemExit(0)

direct = 'prepared' in result
original = sf.read(result['prepared'], dtype='float32', always_2d=True)[0] if direct else raw[36000:252000]
metrics = []
for output in result['outputs']:
    path = Path(output['path'])
    audio, sr = sf.read(path, dtype='float32', always_2d=True)
    report = json.loads(Path(str(path)+'.edit.json').read_text(encoding='utf-8-sig'))
    assert sr == rate and len(audio) == report['edited_frames']
    mode = output['mode']
    metric = dict(mode=mode, frames=len(audio), path=str(path))
    if direct:
        expected={'halve_1':372000,'halve_2':360000,'halve_3':348000,'replace_cut':336000,'sync':384000}[mode]
        assert len(audio)==expected,(mode,len(audio),expected)
        max_delta=0.0
        for seg in ([] if mode=='sync' else report['time_map']):
            a,b,c,d=(seg[k] for k in ('original_start','original_end','edited_start','edited_end'))
            delta=float(np.max(np.abs(audio[c:d]-original[a:b]))) if b>a else 0.0
            max_delta=max(max_delta,delta)
        assert max_delta==0.0,(mode,max_delta)
        metric['conserved_pcm_max_delta']=max_delta
        if mode=='sync':
            spans=[(54000,66000),(168000,192000),(282000,294000)]
            mask=np.ones(len(audio),dtype=bool)
            for a,b in spans:
                assert np.count_nonzero(audio[a:b])==0,(a,b)
                mask[a:b]=False
            assert np.array_equal(audio[mask],original[mask])
            assert np.array_equal(audio[300000:],original[300000:])
            metric['following_pcm_max_delta']=0.0
    elif mode in ('attenuate', 'silence'):
        assert audio.shape == original.shape
        a, b = 38400, 60000
        outside = np.concatenate((audio[:a]-original[:a], audio[b:]-original[b:]))
        delta = float(np.max(np.abs(outside)))
        assert delta == 0, (mode, delta)
        expected = 10**(-12/20) if mode == 'attenuate' else 0
        core = slice(a+1000,b-1000)
        error = float(np.max(np.abs(audio[core]-original[core]*expected)))
        assert error < 1e-8, (mode, error)
        metric.update(outside_max_delta=delta, core_gain_error=error)
    else:
        expected = 194400 if mode == 'cut' else 205200
        assert len(audio) == expected, (mode, len(audio), expected)
        # Verify conserved segments against immutable source, away from fades.
        for segment in report['time_map']:
            a,b,c,d = (segment[k] for k in ('original_start','original_end','edited_start','edited_end'))
            trim = min(1000,(b-a)//3)
            if b-a > 2*trim:
                assert np.array_equal(audio[c+trim:d-trim], original[a+trim:b-trim])
    metrics.append(metric)
summary = dict(status='passed',source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),metrics=metrics)
destination = args.report.with_name('audio_validation.json')
destination.write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
