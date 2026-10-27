# SPDX-License-Identifier: MPL-2.0
"""Test del motore di editing condiviso (Taglia, Dimezza, Attenua, TimeMap).

Esegue bc_studio_cli in una cartella temporanea, su un WAV sintetico con
respiri noti generato da bc_cli. Non usa audio utente e non richiede REAPER.
"""
import hashlib
import json
import os
import shutil
import struct
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


def run(*args, check=True):
    result = subprocess.run(args, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(f"comando fallito {args}:\n{result.stdout}\n{result.stderr}")
    return result


def make_plan(path, sr, frames, events, markers=None, fade_ms=0.0, crossfade_ms=0.0):
    plan = {
        "schema": 2,
        "source": path,
        "sample_rate": sr,
        "channels": 1,
        "reduction_db": -12,
        "fade_ms": fade_ms,
        "crossfade_ms": crossfade_ms,
        "marker_scope": "voice",
        "detector": "dsp",
        "events": events,
        "markers": markers or [],
    }
    json.dump(plan, open(os.path.join(os.path.dirname(path), "plan.json"), "w"), indent=2)


def event(start, end, mode, approved=True, keep=0.5, fade=0.0, margin_ms=0.0):
    return {"start": start, "end": end, "score": 0.8, "enabled": True,
            "approved": approved, "state": "manual", "mode": mode,
            "keep_fraction": keep, "fade_ms": fade,
            "safety_margin_ms": margin_ms}


def report_for(outdir):
    reports = [p for p in os.listdir(outdir) if p.endswith(".edit.json")]
    assert len(reports) == 1, reports
    return json.load(open(os.path.join(outdir, reports[0]), encoding="utf-8"))


def output_wav(outdir):
    files = [p for p in os.listdir(outdir) if p.endswith(".wav")]
    assert len(files) == 1, files
    return os.path.join(outdir, files[0])


def expected_concat(original, frames, kept):
    chunks = [original[a:b] for a, b in kept]
    return np.concatenate(chunks) if chunks else np.zeros(0, dtype=original.dtype)


def main():
    if not os.path.exists(CLI):
        print(f"CLI non trovata: {CLI}")
        return 2
    temp = tempfile.mkdtemp(prefix="bc_edit_test_")
    try:
        source = os.path.join(temp, "gen.wav")
        run(GEN, "gen-test", source)
        original, sr = sf.read(source, dtype="int16")
        original_hash = sha(source)
        frames = len(original)
        events = [
            {"start": 89456, "end": 100304, "score": 0.6, "enabled": True,
             "approved": True, "state": "manual", "mode": "cut",
             "keep_fraction": 0.5, "fade_ms": 0.0},
            {"start": 175984, "end": 186592, "score": 0.6, "enabled": True,
             "approved": True, "state": "manual", "mode": "cut",
             "keep_fraction": 0.5, "fade_ms": 0.0},
            {"start": 262240, "end": 273088, "score": 0.6, "enabled": True,
             "approved": True, "state": "manual", "mode": "cut",
             "keep_fraction": 0.5, "fade_ms": 0.0},
        ]
        markers = [
            {"time": 95000, "end": 95000, "is_region": False, "name": "dentro"},
            {"time": 170000, "end": 190000, "is_region": True, "name": "regione"},
            {"time": 300000, "end": 300000, "is_region": False, "name": "dopo"},
        ]
        make_plan(source, sr, frames, events, markers)
        outdir = os.path.join(temp, "cut")
        run(CLI, "edit", source, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        report = report_for(outdir)
        expected_removed = sum(e["end"] - e["start"] for e in events)
        assert report["removed_samples"] == expected_removed, report
        assert report["edited_frames"] == frames - expected_removed, report
        out, out_sr = sf.read(output_wav(outdir), dtype="int16")
        assert out_sr == sr
        chunks = parse_riff(output_wav(outdir))
        assert chunks[0][0] == "fmt ", chunks
        assert chunks[-1][0] == "data", chunks
        kept = []
        cursor = 0
        for e in events:
            kept.append((cursor, e["start"]))
            cursor = e["end"]
        kept.append((cursor, frames))
        expected = expected_concat(original, frames, kept)
        assert len(out) == len(expected), (len(out), len(expected))
        assert np.max(np.abs(out.astype(np.int32) - expected.astype(np.int32))) <= 1
        assert report["time_map"] == [
            {"original_start": 0, "original_end": 89456,
             "edited_start": 0, "edited_end": 89456},
            {"original_start": 100304, "original_end": 175984,
             "edited_start": 89456, "edited_end": 165136},
            {"original_start": 186592, "original_end": 262240,
             "edited_start": 165136, "edited_end": 240784},
            {"original_start": 273088, "original_end": frames,
             "edited_start": 240784, "edited_end": frames - expected_removed},
        ]
        assert "dentro taglio" in report["markers"][0]["name"]
        assert sha(source) == original_hash
        print("OK cut multipli, durata, TimeMap, marker, sorgente invariata")

        # Dimezza al 50%: rimuove la parte centrale di ogni evento.
        half_events = []
        for e in events:
            e = dict(e)
            e["mode"] = "halve"
            e["keep_fraction"] = 0.5
            half_events.append(e)
        make_plan(source, sr, frames, half_events)
        outdir = os.path.join(temp, "halve")
        run(CLI, "edit", source, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        report = report_for(outdir)
        expected_half = 0
        for e in half_events:
            length = e["end"] - e["start"]
            keep = int(round(length * 0.5))
            expected_half += length - keep
        assert report["removed_samples"] == expected_half, report
        assert sha(source) == original_hash
        print("OK dimezza 50%, durata, sorgente invariata")

        # Percentuali 25/50/75: la percentuale e' la frazione del nucleo
        # tagliabile che resta, quindi la rimozione e' (1-p)*core.
        for percent in (0.25, 0.50, 0.75):
            pct_events = [dict(e) for e in half_events]
            for e in pct_events:
                e["keep_fraction"] = percent
            make_plan(source, sr, frames, pct_events)
            outdir = os.path.join(temp, f"pct_{int(percent * 100)}")
            run(CLI, "edit", source, "--out-dir", outdir, "--plan",
                os.path.join(temp, "plan.json"))
            report = report_for(outdir)
            expected = 0
            for e in pct_events:
                core = e["end"] - e["start"]
                expected += core - int(round(core * percent))
            assert report["removed_samples"] == expected, (percent, report)
            print(f"OK dimezza {int(percent * 100)}%: rimossi {expected} campioni")

        # Margine di sicurezza: il taglio non tocca i primi/ultimi margin ms.
        margin_ms = 10.0
        margin_frames = int(round(margin_ms * sr / 1000.0))
        margin_events = [dict(e) for e in events[:1]]
        margin_events[0]["safety_margin_ms"] = margin_ms
        make_plan(source, sr, frames, margin_events)
        outdir = os.path.join(temp, "margin")
        run(CLI, "edit", source, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        report = report_for(outdir)
        core = margin_events[0]["end"] - margin_events[0]["start"] - 2 * margin_frames
        assert report["removed_samples"] == core, report
        assert report["operations"][0]["safety_margin_samples"] == margin_frames, report
        print("OK margine di sicurezza e formula Taglia")

        # Respiro piu' breve del margine: non deve essere tagliato.
        short = event(1000, 1000 + 2 * margin_frames - 4, "cut",
                      margin_ms=margin_ms)
        make_plan(source, sr, frames, [short])
        outdir = os.path.join(temp, "short")
        run(CLI, "edit", source, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        report = report_for(outdir)
        assert report["removed_samples"] == 0, report
        assert report["rejected_events"] == 1, report
        print("OK respiro piu' breve del margine non tagliato")

        # Crossfade: con fade 10 ms il raccordo non deve produrre un salto
        # maggiore di un click sintetico; si misura il massimo salto attorno
        # ai punti di giunzione.
        fade_events = [dict(e) for e in events]
        for e in fade_events:
            e["fade_ms"] = 10.0
        make_plan(source, sr, frames, fade_events)
        outdir = os.path.join(temp, "crossfade")
        run(CLI, "edit", source, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        out, _ = sf.read(output_wav(outdir), dtype="float32")
        report = report_for(outdir)
        for segment in report["time_map"]:
            pos = segment["edited_start"]
            if 0 < pos < len(out):
                window = out[max(0, pos - 200):min(len(out), pos + 200)]
                jumps = np.abs(np.diff(window))
                assert np.max(jumps) < 0.2, (pos, np.max(jumps))
        print("OK crossfade/raccordo senza salti anomali")

        # Crossfade con sovrapposizione: 3 giunzioni da 5 ms a 48 kHz.
        # La durata cala di 3*240 campioni oltre al taglio; il report lo dice.
        outdir = os.path.join(temp, "overlap_crossfade")
        # Explicitly approve the crossfade handles as part of each region.
        handle=int(round(.005*sr))
        crossfade_events=[dict(e,start=e['start']-handle,end=e['end']+handle,safety_margin_ms=5.0) for e in events]
        make_plan(source, sr, frames, crossfade_events, crossfade_ms=5.0)
        run(CLI, "edit", source, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        report = report_for(outdir)
        expected_crossfade = 3 * int(round(0.005 * sr))
        assert report["crossfade_frames"] == expected_crossfade, report
        assert report["removed_samples"] == expected_removed + expected_crossfade, report
        assert report["edited_frames"] == frames - expected_removed - expected_crossfade, report
        assert len(report["voice_markers"]) == len(report["project_markers"]), report
        assert sha(source) == original_hash
        print("OK crossfade con overlap e contabilita' campioni")

        # Evento fuori dai limiti: viene rifiutato, il resto resta valido.
        bad_events = [dict(events[0]), dict(events[1])]
        bad_events[1]["end"] = frames + 1000
        make_plan(source, sr, frames, bad_events)
        outdir = os.path.join(temp, "bad")
        run(CLI, "edit", source, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        report = report_for(outdir)
        assert report["rejected_events"] == 1, report
        assert report["applied_events"] == 1, report
        print("OK evento fuori limiti rifiutato")

        # Sovrapposizione: il piano deve fallire prima di scrivere.
        overlap = [dict(events[0]), dict(events[1])]
        overlap[1]["start"] = 95000
        overlap[1]["end"] = 105000
        make_plan(source, sr, frames, overlap)
        outdir = os.path.join(temp, "overlap")
        result = run(CLI, "edit", source, "--out-dir", outdir, "--plan",
                     os.path.join(temp, "plan.json"), check=False)
        assert result.returncode != 0
        assert "sovrappongono" in (result.stdout + result.stderr)
        assert not os.path.exists(os.path.join(outdir, "gen_cut.wav"))
        print("OK sovrapposizione rifiutata")

        # BWF: bext deve sopravvivere alla scrittura della copia tagliata.
        raw = open(source, "rb").read()
        pos = 12
        fmt = data = None
        while pos + 8 <= len(raw):
            cid = raw[pos:pos + 4]
            size = struct.unpack("<I", raw[pos + 4:pos + 8])[0]
            payload = raw[pos + 8:pos + 8 + size]
            if cid == b"fmt ":
                fmt = payload
            elif cid == b"data":
                data = payload
            pos += 8 + size + (size & 1)
        bext = (b"BWF-TEST-" + b"0" * 40).ljust(602, b"\0")
        cue = b"\0\0\0\0" + b"\0" * 24  # cue point fittizio: non va ricopiato
        junk = b"JUNK-TEST-" + b"0" * 4
        body = b"WAVE"
        for cid, payload in ((b"bext", bext), (b"fmt ", fmt), (b"junk", junk),
                             (b"cue ", cue), (b"data", data)):
            body += cid + struct.pack("<I", len(payload)) + payload
            if len(payload) & 1:
                body += b"\0"
        bwf = os.path.join(temp, "bwf.wav")
        open(bwf, "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)
        bwf_hash = sha(bwf)
        make_plan(bwf, sr, len(original), [dict(events[0])])
        outdir = os.path.join(temp, "bwf")
        run(CLI, "edit", bwf, "--out-dir", outdir, "--plan",
            os.path.join(temp, "plan.json"))
        outraw = open(output_wav(outdir), "rb").read()
        assert b"bext" in outraw
        assert bext[:40] in outraw
        assert sha(bwf) == bwf_hash
        bwf_chunks = parse_riff(bwf)
        out_chunks = parse_riff(output_wav(outdir))
        assert bwf_chunks[0][0] == "bext", bwf_chunks
        assert out_chunks[0][0] == "bext", out_chunks
        assert "bext" in [c[0] for c in out_chunks]
        # data PCM16 pari: nessun padding necessario; per PCM24 mono il test
        # del padding e' in test_studio_pipeline.py.
        assert os.path.getsize(output_wav(outdir)) > 0
        assert "cue " not in [c[0] for c in out_chunks]
        assert "junk" in [c[0] for c in out_chunks]
        edit_json = json.load(open(os.path.join(
            outdir, [n for n in os.listdir(outdir) if n.endswith(".edit.json")][0]),
            encoding="utf-8"))
        notes = {c["id"]: c["note"] for c in edit_json["wav_chunks"]}
        assert "cue " in notes, notes
        assert "non piu' validi" in notes["cue "], notes
        assert edit_json["wav_padding"] in (True, False)
        print("OK chunk BWF conservato, sorgente invariata")

        print("EDIT ENGINE TEST OK")
        return 0
    finally:
        shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
