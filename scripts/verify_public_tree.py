# SPDX-License-Identifier: MPL-2.0
"""Audit every object reachable from an explicitly selected publication ref.

Read-only: no fetch, push, history rewriting or credential output.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', required=True)
    parser.add_argument('--base', required=True, help='Previously inspected remote main')
    parser.add_argument('--private-blob-ids', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    revision = git('rev-parse', '--verify', args.ref + '^{commit}').decode().strip()
    base = git('rev-parse', '--verify', args.base + '^{commit}').decode().strip()
    parents = git('rev-list', '--parents', '-1', revision).decode().split()
    if parents != [revision, base]:
        raise SystemExit('Publication must have exactly the inspected remote base as parent')
    denied = set()
    if args.private_blob_ids:
        denied.update(json.loads(args.private_blob_ids.read_text(encoding='utf-8-sig')))
    forbidden_path = re.compile(
        r'(^|/)(build(?:_[^/]+)?|dist|dataset[s]?|AI_Fiati|materiale_flat|__pycache__|\.env[^/]*)(/|$)'
        r'|\.(wav|flac|mp3|onnx(?:\.data)?|dll|exe|lib|obj|pdb|zip|bundle|pt|pth|ckpt|pyc)$'
        r'|(^|/)(pesi\.json|rete\.json|breath_model(?:_net|_net_narrow)?\.h)$', re.I)
    secrets = {
        'github_token': re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{30,})'),
        'private_key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
        'aws_access_key': re.compile(r'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
        'local_workspace': re.compile(r'K:[\\/]+AI[\\/]+Auto_Editing', re.I),
        'personal_recording_name': re.compile(r'\b01-2610\d{2}_\d{4}(?:[-.]|\.wav)'),
    }
    objects = []
    issues = []
    listing = git('rev-list', '--objects', revision).decode('utf-8').splitlines()
    metadata = subprocess.check_output(
        ['git', '-C', str(ROOT), 'cat-file', '--batch-check=%(objecttype) %(objectsize)'],
        input=('\n'.join(line.split(' ', 1)[0] for line in listing) + '\n').encode())
    for line, meta in zip(listing, metadata.decode().splitlines()):
        oid, _, name = line.partition(' ')
        kind, size_text = meta.split()
        size = int(size_text)
        objects.append({'oid': oid, 'type': kind, 'bytes': size, 'path': name})
        if oid in denied:
            issues.append({'path': name, 'reason': 'private model blob reachable'})
        if kind != 'blob':
            continue
        if forbidden_path.search(name):
            issues.append({'path': name, 'reason': 'excluded artifact path'})
        if size > 4 * 1024 * 1024:
            issues.append({'path': name, 'reason': 'unexpected large blob'})
        content = git('cat-file', 'blob', oid)
        if b'\0' in content:
            if not name.lower().endswith(('.ico', '.png')):
                issues.append({'path': name, 'reason': 'unexpected binary payload'})
            continue
        text = content.decode('utf-8', errors='replace')
        for label, pattern in secrets.items():
            if pattern.search(text):
                # Report only the reason and path, never a potential credential.
                issues.append({'path': name, 'reason': label})
    files = git('ls-tree', '-r', '--name-only', revision).decode().splitlines()
    report = {'status': 'failed' if issues else 'passed', 'ref': args.ref,
              'commit': revision, 'base': base, 'files': len(files),
              'commits': int(git('rev-list', '--count', revision)),
              'payload_bytes': sum(o['bytes'] for o in objects if o['type'] == 'blob'),
              'objects': objects, 'issues': issues}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('status', 'commit', 'files', 'commits', 'payload_bytes', 'issues')}))
    return bool(issues)


if __name__ == '__main__':
    raise SystemExit(main())
