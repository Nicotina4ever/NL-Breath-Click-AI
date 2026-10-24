# SPDX-License-Identifier: MPL-2.0
"""Explicit CI-only retrieval of hash-pinned public AI fixtures/native runtimes.

Never invoked by the application or CMake. No private model or Python package
installation: only native ORT libraries are selected from the official wheel.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import urllib.request
import zipfile

RELEASE = "https://github.com/Nicotina4ever/NL-Breath-Click-AI/releases/download/v1.0.0-rc.2/NL_Breath_Click_Studio_REAPER_Windows_Beta_1.0.0-rc.2.zip"
RELEASE_SHA = "0488130767cb05e10a0492d7445489b7f5a9af6051ec402b1d2cd67f241f8815"
WHEELS = {
    ("Linux", "x86_64"): ("onnxruntime-1.31.0-cp311-cp311-manylinux_2_28_x86_64.whl", "d4092b78fc5bab77ce6522393098cdb2535423045ecdcff15cc0d022162d6b66"),
    ("Darwin", "arm64"): ("onnxruntime-1.31.0-cp311-cp311-macosx_14_0_arm64.whl", "cbf1a7f6470ddfe9dbc781966af8ce4a10e1858d75a93f93cc6b9367c9587870"),
}

def archive(url, expected):
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read(256 * 1024 * 1024)
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("Downloaded archive SHA-256 mismatch")
    return zipfile.ZipFile(io.BytesIO(data))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    lock = json.loads((Path(__file__).resolve().parents[1] / "docs/DEPENDENCIES.lock.json").read_text(encoding="utf-8"))
    pins = {entry["path"]: entry["sha256"] for entry in lock["files"]}
    with archive(RELEASE, RELEASE_SHA) as package:
        selected = [("models/generale/" + name, root / "models" / name) for name in ("respiro-en-dinamico.onnx", "respiro-en-dinamico.onnx.data")]
        if platform.system() == "Windows":
            selected.append(("third_party/onnxruntime/onnxruntime.dll", root / "onnxruntime.dll"))
        for source, target in selected:
            member = "bin/" + (source.split("/")[-1] if source.startswith("third_party/") else source)
            matches = [name for name in package.namelist() if name == member or name.endswith("/" + member)]
            if len(matches) != 1:
                raise ValueError("Missing or ambiguous public runtime member: " + member)
            data = package.read(matches[0])
            if hashlib.sha256(data).hexdigest() != pins[source]:
                raise ValueError("Pinned payload mismatch: " + source)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    if platform.system() != "Windows":
        filename, digest = WHEELS[(platform.system(), platform.machine())]
        with urllib.request.urlopen("https://pypi.org/pypi/onnxruntime/1.31.0/json", timeout=30) as response:
            metadata = json.load(response)
        entry = next(item for item in metadata["urls"] if item["filename"] == filename)
        if entry["digests"]["sha256"] != digest or not entry["url"].startswith("https://files.pythonhosted.org/"):
            raise ValueError("Unexpected official ORT wheel")
        with archive(entry["url"], digest) as wheel:
            suffix = ".dylib" if platform.system() == "Darwin" else ".so"
            matches = [name for name in wheel.namelist() if (Path(name).name.startswith("libonnxruntime.") and (Path(name).name.endswith(".dylib") if suffix == ".dylib" else ".so" in Path(name).name))]
            if len(matches) != 1:
                raise ValueError("Missing or ambiguous native ORT library")
            (root / ("libonnxruntime" + suffix)).write_bytes(wheel.read(matches[0]))
    print("Pinned public model and native ONNX Runtime 1.31.0 prepared:", platform.system(), platform.machine())

if __name__ == "__main__":
    main()
