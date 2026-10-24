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
