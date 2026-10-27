# SPDX-License-Identifier: MPL-2.0
"""Import/session/export using shipped executables and synthetic WAV/MP3/FLAC.

Python generates/inspects fixtures only; the application runs as a separate
native process. Also tests a package copy with the optional LAME DLL absent.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_paths import CLI, BIN


def run(*args, check=True):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, encoding="utf-8", timeout=90)
    if check and result.returncode:
        raise RuntimeError(f"{args}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


def probe(path):
    return dict(line.split("=", 1) for line in run(CLI, "probe", path).stdout.splitlines() if "=" in line)


def digest(path):
    return hashlib.sha256(native_path(path).read_bytes()).hexdigest()


def native_path(path):
    """Test-side IO works beyond MAX_PATH without changing Windows policy.

    Pass ordinary, unprefixed paths to BreathClick to exercise its own IO.
    """
    text = os.path.abspath(path)
    if os.name != "nt" or text.startswith("\\\\?\\"):
        return Path(text)
    return Path("\\\\?\\UNC\\" + text[2:] if text.startswith("\\\\") else "\\\\?\\" + text)


def main():
    absent = "--codec-absent" in sys.argv
    real = None
    if '--real-fixture' in sys.argv:
        real = Path(sys.argv[sys.argv.index('--real-fixture')+1]).resolve()
        repo = Path(__file__).resolve().parents[2]
        if not real.is_relative_to(repo/'build'):
            raise ValueError('Real voice tests require an authorized build-local copy')
        real_hash = digest(real)
    with tempfile.TemporaryDirectory(prefix="bc_portable_audio_") as directory:
        work = Path(directory)
        sr = 48000
        t = np.arange(sr * 2) / sr
        voice = .08 * np.sin(2 * np.pi * 180 * t)
        stereo = np.column_stack([voice, voice * .7])
        if real:
            stereo, sr = sf.read(real, dtype='float32', always_2d=True)
            assert len(stereo)>30000
        long_work = work / ("percorso_" + "a" * 90) / ("voce_è_🎙_" + "b" * 90)
        native_path(long_work).mkdir(parents=True)
        try:
            cli_fixture = long_work / "sintetica_è.wav"
            assert len(str(cli_fixture)) > 260
            run(BIN / "bc_cli.exe", "gen-test", cli_fixture, "--quiet")
            assert probe(cli_fixture)["format"] == "WAV"
            for extension, subtype in (("wav", "PCM_24"), ("flac", "PCM_24"), ("mp3", "MPEG_LAYER_III")):
                fixture = work / f"source.{extension}"
                sf.write(fixture, stereo, sr, subtype=subtype)
                for location in (work, long_work):
                    source = location / f"source.{extension}"
                    if location != work:
                        shutil.copyfile(native_path(fixture), native_path(source))
                    original = digest(source)
                    before = probe(source)
                    assert int(before["channels"]) == stereo.shape[1] and int(before["sample_rate"]) == sr, before
                    session = location / f"{extension}.bcnstudio.json"
                    native_path(session).write_text(json.dumps({
                        "schema": 2, "source": str(source), "source_sha256": original,
                        "regions": [{"kind": 0, "start": 10000, "end": 30000,
                                "approved": True, "enabled": True, "from_detection": True,
                                "reduction_db": -12, "edit_mode": 0, "fade_ms": 15,
                                "decision_source": "automatic", "training_eligible": False}],
                        "events": [], "corrections": []
                    }), encoding="utf-8")
                    out = location / f"out_{extension}"
                    result = run(BIN / "bc_studio.exe", "--session-export", session, out, check=False)
                    outputs = list(native_path(out).glob("*.mp3")) + list(native_path(out).glob("*.wav"))
                    if absent and extension == "mp3":
                        assert result.returncode != 0 and not outputs, (result.returncode, outputs)
                        report = native_path(out / "breathclick_export_report.json").read_text(encoding="utf-8")
                        assert "libmp3lame" in report or "encoder" in report, report
                    else:
                        assert result.returncode == 0 and len(outputs) == 1, (result.returncode, outputs, result.stderr)
                        after = probe(outputs[0])
                        assert after["channels"] == before["channels"] and after["sample_rate"] == before["sample_rate"], after
                        if extension != "mp3":
                            assert after["frames"] == before["frames"], (extension, before, after)
                        else:
                            assert abs(int(after["frames"]) - int(before["frames"])) <= int(sr * .03), (before, after)
                    assert digest(source) == original, f"Original changed: {source}"
                    print(f"PORTABLE AUDIO OK: {extension}, channels={stereo.shape[1]}, duration, original hash; codec_absent={absent}, real_voice={real is not None}, long_path={location == long_work}")
        finally:
            # Only this TemporaryDirectory's synthetic fixtures, via extended
            # paths so cleanup does not require a global long-path policy.
            shutil.rmtree(native_path(work))
    if real:
        assert digest(real)==real_hash
    return 0


if __name__ == "__main__":
    sys.exit(main())
