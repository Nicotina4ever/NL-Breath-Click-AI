# SPDX-License-Identifier: MPL-2.0
"""Test riproducibili della pipeline NL Breath Click Studio (senza REAPER).

Genera varianti PCM/float mono/stereo di un WAV sintetico con respiri noti,
le elabora con bc_studio_cli e verifica formato, durata, attenuazione e hash
dell'originale. Non usa audio utente: lavora in una cartella temporanea.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from riff_strict import parse as parse_riff

from test_paths import CLI, GEN



def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"comando fallito {args}:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    if not os.path.exists(CLI):
        print(f"CLI non trovata: {CLI}")
        return 2
    temp = tempfile.mkdtemp(prefix="bc_studio_test_")
    try:
        base = os.path.join(temp, "gen.wav")
        if os.path.exists(GEN):
            run(GEN, "gen-test", base)
        else:
            # Fallback: rumore con tre brevi bande di respiro.
            sr = 48000
            t = np.arange(sr * 6) / sr
            y = np.random.default_rng(1).normal(0, 0.0005, t.shape[0])
            for start in (1.8, 3.6, 5.4):
                i = int(start * sr)
                n = int(0.25 * sr)
                y[i:i + n] += np.random.default_rng(2).normal(0, 0.08, n) * np.hanning(n)
            sf.write(base, y, sr, subtype="PCM_16")

        original_hash = sha(base)
        data, sr = sf.read(base)
        mono = data if data.ndim == 1 else data[:, 0]
        variants = {
            "pcm16_mono": ("PCM_16", mono),
            "pcm24_mono": ("PCM_24", mono),
            "pcm24_stereo": ("PCM_24", np.column_stack([mono, mono])),
            "float32_stereo": ("FLOAT", np.column_stack([mono, mono])),
        }
        reports = []
        for name, (subtype, signal) in variants.items():
            source = os.path.join(temp, name + ".wav")
            outdir = os.path.join(temp, "out_" + name)
            sf.write(source, signal, sr, subtype=subtype)
            before = sha(source)
            run(CLI, "process", source, "--out-dir", outdir,
                "--detector", "dsp", "--reduction-db", "-12", "--fade-ms", "15")
            output = os.path.join(outdir, name + "_breathclick.wav")
            assert os.path.exists(output), output
            out, out_sr = sf.read(output)
            assert out_sr == sr, (out_sr, sr)
            expected_channels = 1 if name in ("pcm16_mono", "pcm24_mono") else 2
            assert (1 if out.ndim == 1 else out.shape[1]) == expected_channels, name
            assert abs(out.shape[0] - data.shape[0]) <= 2, (out.shape, data.shape)
            assert sha(source) == before, "il file originale e' stato modificato"
            report_path = output + ".report.json"
            report = json.load(open(report_path, encoding="utf-8"))
            assert report["processed_events"] >= 1, report
            chunks = parse_riff(output)
            assert chunks[0][0] == "fmt ", chunks
            assert chunks[-1][0] == "data", chunks
            if name == "pcm24_mono":
                # data dispari: il padding RIFF deve rendere il file coerente
                assert report["wav_padding"] is True, report
                assert os.path.getsize(output) == os.path.getsize(source), (
                    os.path.getsize(output), os.path.getsize(source))
            for event in report["events"]:
                assert abs(event["delta_db"] + 12.0) < 2.0, event
            assert report["outside_max_delta_db"] < 0.25, report
            reports.append({"variant": name, "events": report["processed_events"],
                            "outside_max_delta_db": report["outside_max_delta_db"]})
            print(f"OK {name}: {report['processed_events']} eventi, "
                  f"fuori max {report['outside_max_delta_db']:.3f} dB")

        # Verifica che il runtime ONNX e il modello Respiro-en si carichino
        # davvero: il test non pretende eventi su audio sintetico, ma il
        # comando deve riuscire e dichiarare il detector richiesto.
        for detector in ("ai", "fusion"):
            json_path = os.path.join(temp, detector + ".json")
            run(CLI, "analyze", base, "--detector", detector, "--json", json_path)
            data = json.load(open(json_path, encoding="utf-8"))
            assert data["detector"] == detector, data["detector"]
            assert data["ai_inference_frames"] > 0, "ONNX inference not executed"
            print(f"OK {detector}: runtime ONNX caricato, {len(data['events'])} eventi")

        missing = subprocess.run([CLI, "analyze", base, "--detector", "fusion",
                                  "--model", os.path.join(temp, "missing.onnx")],
                                 capture_output=True, text=True, timeout=60)
        assert missing.returncode != 0 and "Respiro-en" in missing.stdout + missing.stderr
        print("OK Fusion: missing explicit model fails without silent substitution")

        assert sha(base) == original_hash
        print("PIPELINE TEST OK")
        print(json.dumps(reports, indent=2))
        return 0
    finally:
        shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
