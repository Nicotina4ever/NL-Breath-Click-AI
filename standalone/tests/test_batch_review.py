# SPDX-License-Identifier: MPL-2.0
"""Test della coda batch revisionata: solo file con piano vengono applicati.

Usa piani sintetici; fixture esterne soltanto se fornite esplicitamente.
Non modifica gli originali.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_paths import CLI, GEN, INPUT, WORK
PLANS = WORK / "review_plans"


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    global INPUT, PLANS
    if not os.path.exists(CLI):
        raise RuntimeError(f"CLI required: {CLI}")
    temp = tempfile.mkdtemp(prefix="bc_batch_review_")
    if not all(os.path.exists(p) for p in (INPUT, PLANS)):
        from pathlib import Path
        INPUT, PLANS = Path(temp) / "input", Path(temp) / "plans"
        INPUT.mkdir(); PLANS.mkdir()
        for index in range(4):
            source = INPUT / f"synthetic_{index}.wav"
            subprocess.run([GEN, "gen-test", source, "--quiet"], check=True, capture_output=True)
            if index < 2:
                plan = {"schema": 2, "source": str(source), "sample_rate": 48000,
                        "channels": 1, "reduction_db": -12, "fade_ms": 0,
                        "events": [{"start": 48000, "end": 60000, "approved": True,
                                    "enabled": True, "mode": "attenuate",
                                    "decision_source": "review_harness", "training_eligible": False}]}
                (PLANS / f"synthetic_{index}.plan.json").write_text(json.dumps(plan), encoding="utf-8")
        print("Fixture: synthetic reviewed queue (no private plans required)")
    before = {n: sha(os.path.join(INPUT, n)) for n in os.listdir(INPUT) if n.endswith(".wav")}
    try:
        result = subprocess.run(
            [CLI, "batch-review", INPUT, "--out-dir", temp, "--plans", PLANS],
            capture_output=True, text=True)
        assert result.returncode == 0, (result.stdout, result.stderr)
        report_path = os.path.join(temp, "batch_review_report.json")
        report = json.load(open(report_path, encoding="utf-8"))
        assert report["applied"] == 2, report
        assert report["skipped"] >= 2, report
        statuses = {item["status"] for item in report["files"]}
        assert "applicato" in statuses and "da_rivedere" in statuses, statuses
        for name, digest in before.items():
            assert sha(os.path.join(INPUT, name)) == digest, name
        wavs = [n for n in os.listdir(temp) if n.endswith(".wav")]
        assert len(wavs) == 2, wavs
        from riff_strict import parse
        for name in wavs:
            parse(os.path.join(temp, name))
        print("BATCH REVIEW TEST OK", report["applied"], report["skipped"])
        return 0
    finally:
        shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
