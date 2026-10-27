# SPDX-License-Identifier: MPL-2.0
"""Shared release contracts. Packaging never discovers arbitrary user data."""
import hashlib
import json
from pathlib import PurePosixPath
REPOSITORY = 'Nicotina4ever/NL-Breath-Click-AI'
VERSION = '1.0.0-rc.3'
TAG = 'v' + VERSION
REAPER_FILES = ('BreathClickStudio.lua', 'bc_studio_bridge.lua', 'bc_direct.lua',
                'bc_direct_view.lua', 'bcnext_core.lua', 'BreathClickNext_config.lua',
                'bc_json.lua', 'bc_job.ps1', 'bc_runtime.lua', 'bc_runtime.ps1', 'LEGGIMI.txt')

def digest(data):
    return hashlib.sha256(data).hexdigest()

def verify_archive(archive):
    names = archive.namelist()
    if len(names) != len(set(names)) or len(names) > 512:
        raise ValueError('Duplicate or excessive archive entries')
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
            raise ValueError('Unsafe archive path: ' + name)
    manifest = json.loads(archive.read('PACKAGE_MANIFEST.json').decode('utf-8-sig'))
    listed = set()
    for entry in manifest:
        name = entry['path']
        if name in listed or name not in names:
            raise ValueError('Invalid package manifest')
        listed.add(name)
        payload = archive.read(name)
        if len(payload) != entry['bytes'] or digest(payload) != entry['sha256']:
            raise ValueError('Package integrity failed: ' + name)
    if listed | {'PACKAGE_MANIFEST.json'} != {n for n in names if not n.endswith('/')}:
        raise ValueError('Unmanifested package payload')
    return manifest
