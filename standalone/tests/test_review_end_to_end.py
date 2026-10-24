# SPDX-License-Identifier: MPL-2.0
"""Test end-to-end della revisione: analizza, rifiuta 10,036, salva e
riapre la sessione, esporta Attenua/Taglia/Dimezza e verifica che il contenuto
rifiutato resti identico usando la TimeMap (non i timecode del file tagliato).
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_paths import CLI, GEN, INPUT, WORK
from riff_strict import parse as parse_riff

SOURCE = Path(os.environ.get("BC_REVIEW_SOURCE", str(INPUT / "review_input.wav")))
REJECT_SECONDS = 10.036


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def map_original(report, sample):
    for segment in report["time_map"]:
        if segment["original_start"] <= sample < segment["original_end"]:
            return segment["edited_start"] + sample - segment["original_start"]
    return None


def main():
    global SOURCE, REJECT_SECONDS
    if not os.path.exists(CLI):
        raise RuntimeError(f"CLI required: {CLI}")
    temp = tempfile.mkdtemp(prefix="bc_review_e2e_")
    if not os.path.exists(SOURCE):
        SOURCE = os.path.join(temp, "synthetic.wav")
        subprocess.run([GEN, "gen-test", SOURCE, "--quiet"], check=True, capture_output=True)
        proposals = os.path.join(temp, "proposals.json")
        subprocess.run([CLI, "analyze", SOURCE, "--detector", "dsp", "--json", proposals], check=True, capture_output=True)
        with open(proposals, encoding="utf-8") as stream:
            proposal = json.load(stream)["events"][0]
        REJECT_SECONDS = (proposal["start_s"] + proposal["end_s"]) / 2
        print("Fixture: synthetic review (no private recording required)")
    else:
        print("Fixture: existing prepared recording, read only")
    original_hash = sha(SOURCE)
    try:
        result = subprocess.run(
            [CLI, "review", SOURCE, "--reject", str(REJECT_SECONDS),
             "--out-dir", temp, "--detector", "dsp"],
            capture_output=True, text=True)
        assert result.returncode == 0, (result.stdout, result.stderr)
        review = json.load(open(os.path.join(temp, "review.json"), encoding="utf-8"))
        assert review["demo_only"] is True and review["training_eligible"] is False, review
        assert len(review["modes"]) == 3, review
        assert os.path.exists(review["session"]), review
        source, sr = sf.read(SOURCE, dtype="int32")
        reject_sample = int(round(REJECT_SECONDS * sr))
        source_hash_after = sha(SOURCE)
        assert source_hash_after == original_hash
        summary = {"source_sha256": original_hash, "modes": []}
        for item in review["modes"]:
            assert item["rejected_preserved"] is True, item
            assert item["rejected_removed"] is False, item
            assert item["max_abs_diff"] <= 1e-9, item
            edit_report = json.load(open(item["edit_report"], encoding="utf-8"))
            plan = json.load(open(item["plan"], encoding="utf-8"))
            rejected = [e for e in plan["events"]
                        if not e["approved"] and
                        e["start"] - int(0.1 * sr) <= reject_sample <= e["end"] + int(0.1 * sr)]
            assert len(rejected) == 1, rejected
            assert rejected[0]["approved"] is False, rejected
            assert rejected[0].get("decision_source") == "review_harness", rejected
            assert rejected[0].get("training_eligible") is False, rejected
            mapped = map_original(edit_report, reject_sample)
            assert mapped is not None, edit_report
            output, out_sr = sf.read(item["output"], dtype="int32")
            assert out_sr == sr
            # Finestra interna al respiro rifiutato, lontana dai bordi.
            window = max(1, int(0.02 * sr))
            left = max(0, reject_sample - window)
            right = min(len(source), reject_sample + window)
            for s in range(left, right, max(1, window // 8)):
                m = map_original(edit_report, s)
                if m is None or m >= len(output):
                    continue
                diff = abs(int(source[s]) - int(output[m]))
                assert diff <= 2, (item["mode"], s, m, diff)
            chunks = parse_riff(item["output"])
            assert chunks[0][0] == "fmt " and chunks[-1][0] == "data", chunks
            summary["modes"].append({
                "mode": item["mode"],
                "rejected_preserved": item["rejected_preserved"],
                "max_abs_diff": item["max_abs_diff"],
                "removed_samples": item["removed_samples"],
                "chunks": chunks,
                "plan": item["plan"],
                "edit_report": item["edit_report"],
            })
        # La sessione deve essere rileggibile e contenere il rifiuto.
        session = json.load(open(review["session"], encoding="utf-8"))
        assert any(not e["approved"] and
                   e.get("decision_source") == "review_harness" and
                   not e.get("training_eligible", True)
                   for e in session["events"]), session
        summary["session"] = review["session"]
        with open(os.path.join(temp, "review_end_to_end.json"), "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        print("REVIEW END-TO-END OK")
        print(json.dumps(summary, indent=2))
        return 0
    finally:
        shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
