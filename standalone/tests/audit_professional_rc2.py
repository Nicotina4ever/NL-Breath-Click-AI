# SPDX-License-Identifier: MPL-2.0
"""Local, opt-in evidence from COPIES of explicitly selected professional trials.

Never modifies the original recordings, exported reports or recovery state.
Run with --copies build/validation/rc2_real and --bin <tested binaries>.
Private fixtures and outputs stay outside Git; only this verifier is versioned.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import uuid
import numpy as np
import soundfile as sf

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--copies", required=True, type=Path)
    parser.add_argument("--bin", required=True, type=Path)
    parser.add_argument("--case", action="append", required=True, metavar="STEM:TOTAL:INCLUDED")
    args = parser.parse_args()
    work = args.copies.resolve()
    if any(part.casefold() in {"materiale_flat", "ai_fiati", "da_eliminare"} for part in work.parts):
        raise ValueError("Use a copy under build/validation, never a protected data directory")
    binary = args.bin.resolve()
    result = []
    for case in args.case:
        take,total,included_count=case.rsplit(":",2)
        if Path(take).name!=take or any(c in take for c in "/\\"):
            raise ValueError("Case stem must be a filename within the copies directory")
        expected_total,expected_included=int(total),int(included_count)
        source = work / (take + ".wav")
        old_plan_path = work / (take + " - corretto.wav.plan.json")
        old_report_path = work / (take + " - corretto.wav.edit.json")
        recovery = work / ("recovery_" + take + ".wav.json")
        protected = {path: sha(path) for path in (source, old_plan_path, old_report_path, recovery)}
        session = read(recovery)
        plan = read(old_plan_path)
        old_report = read(old_report_path)
        regions = session["regions"]
        included = [r for r in regions if r["enabled"] and r["approved"]]
        key = lambda r: (r.get("kind", 0), r["start"], r["end"])
        assert len(regions) == expected_total and len(included) == expected_included
        assert Counter(map(key, included)) == Counter(map(key, plan["events"])), "old plan differs from included regions"
        assert old_report["applied_events"] == expected_included and old_report["rejected_events"] == 0
        labels = Counter(r.get("learning_label", "") for r in regions)
        assert all(r["training_eligible"] and r["decision_source"]=="user"
                   for r in regions if r.get("learning_label") in ("false_positive","breath_confirmed","missed_breath"))
        # A new session copy refers only to the copied audio; never the original.
        session["source"] = str(source)
        session["source_relative"] = source.name
        session_path = work / (take + ".rc2.bcnstudio.json")
        session_path.write_text(json.dumps(session), encoding="utf-8")
        outdir = work / (take + "_rc2_export_" + uuid.uuid4().hex)
        subprocess.run([str(binary / "bc_studio.exe"), "--session-export", str(session_path), str(outdir)], check=True, timeout=90)
        wavs = list(outdir.glob("*.wav"))
        assert len(wavs) == 1
        exported_plan = read(Path(str(wavs[0]) + ".plan.json"))
        report = read(Path(str(wavs[0]) + ".edit.json"))
        assert Counter(map(key, exported_plan["events"])) == Counter(map(key, included))
        assert report["applied_events"] == expected_included and report["rejected_events"] == 0
        before, sr = sf.read(source, dtype="float64", always_2d=True)
        after, after_sr = sf.read(wavs[0], dtype="float64", always_2d=True)
        assert before.shape == after.shape and sr == after_sr
        mask = np.zeros(len(before), dtype=bool)
        for event in included:
            # A repaired short click can have ramps outside its interval;
            # conservatively exclude the event fade from the unchanged mask.
            pad = int(sr * event.get("fade_ms", 15) / 1000) + 1
            mask[max(0, event["start"] - pad):min(len(mask), event["end"] + pad)] = True
        outside = float(np.max(np.abs(before[~mask] - after[~mask]))) if (~mask).any() else 0
        assert outside <= 2 ** -22, f"unexpected audio outside included intervals: {outside}"
        fusion_path = work / (take + "_fusion_rc2.json")
        subprocess.run([str(binary / "bc_studio_cli.exe"), "analyze", str(source), "--detector", "fusion", "--json", str(fusion_path)], check=True, timeout=90, capture_output=True)
        fusion = read(fusion_path)
        assert fusion["detector"] == "fusion" and fusion["ai_inference_frames"] > 0
        assert any(e.get("ai_score", 0) > 0 or e.get("ai_max_probability", 0) > 0 for e in fusion["events"])
        # Missing model is a controlled error, never silent DSP fallback.
        failure = subprocess.run([str(binary / "bc_studio_cli.exe"), "analyze", str(source), "--detector", "fusion", "--model", str(work / "missing.onnx")], capture_output=True, timeout=90)
        assert failure.returncode != 0 and b"Respiro-en" in failure.stderr + failure.stdout
        assert all(sha(path) == digest for path, digest in protected.items())
        result.append(dict(take=take, displayed=len(regions), included=len(included), excluded=len(regions)-len(included),
                           labels=dict(labels), applied=report["applied_events"], rejected=report["rejected_events"],
                           frames=len(before), unchanged_max_delta=outside, ai_frames=fusion["ai_inference_frames"],
                           fusion_events=len(fusion["events"]), copied_inputs_unchanged=True))
    (work / "RC2_EVIDENCE.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    print("PROFESSIONAL COPIES RC2 OK (no perceptual accuracy claim)")

if __name__ == "__main__":
    main()
