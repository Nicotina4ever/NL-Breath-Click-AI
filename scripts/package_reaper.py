# SPDX-License-Identifier: MPL-2.0
"""Build the independent REAPER ZIP from the exact verified standalone backend."""
import argparse
import json
from pathlib import Path
import zipfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from distribution import REAPER_FILES, REPOSITORY, TAG, VERSION, digest, verify_archive

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--standalone-zip',type=Path,required=True)
    parser.add_argument('--source-commit',required=True)
    parser.add_argument('--output-directory',type=Path,default=Path('dist'))
    args=parser.parse_args();root=Path(__file__).resolve().parents[1]
    payload={}
    with zipfile.ZipFile(args.standalone_zip) as archive:
        manifest=verify_archive(archive)
        info=json.loads(archive.read('BUILD_INFO.json').decode('utf-8-sig'))
        if info['source_commit']!=args.source_commit or info['version']!=VERSION:
            raise SystemExit('Standalone provenance mismatch')
        for entry in manifest:
            name=entry['path']
            if name=='bc_studio_cli.exe' or name.endswith('.dll') or name.startswith('models/'):
                payload['bin/'+name]=archive.read(name)
            elif name in ('LICENSE','THIRD_PARTY_NOTICES.md','DEPENDENCIES.lock.json') or name.startswith(('LICENSES/','sources/')):
                payload[name]=archive.read(name)
    for name in REAPER_FILES:payload[name]=(root/'reaper'/name).read_bytes()
    payload['Install_REAPER.ps1']=(root/'packaging/Install_REAPER.ps1').read_bytes()
    payload['README.txt']=(root/'packaging/REAPER_README.txt').read_bytes()
    info['product']='NL Breath Click Studio - REAPER Windows Beta'
    payload['BUILD_INFO.json']=(json.dumps(info,indent=2)+'\n').encode()
    payload['PACKAGE_MANIFEST.json']=json.dumps([
        dict(path=name,bytes=len(data),sha256=digest(data)) for name,data in sorted(payload.items())
    ],indent=2).encode()
    args.output_directory.mkdir(parents=True,exist_ok=True)
    output=args.output_directory/('NL_Breath_Click_Studio_REAPER_Windows_Beta_'+VERSION+'.zip')
    pending=output.with_suffix('.pending.zip')
    with zipfile.ZipFile(pending,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,data in sorted(payload.items()):archive.writestr(name,data)
    with zipfile.ZipFile(pending) as archive:verify_archive(archive)
    pending.replace(output)
    sha=digest(output.read_bytes())
    output.with_suffix('.zip.sha256').write_text(sha+'  '+output.name+'\n',encoding='ascii')
    runtime=dict(schema=1,version=VERSION,source_commit=args.source_commit,
                 url=f'https://github.com/{REPOSITORY}/releases/download/{TAG}/{output.name}',sha256=sha)
    (args.output_directory/'runtime-release.json').write_text(json.dumps(runtime,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status='passed',zip=str(output),sha256=sha,files=len(payload),source_commit=args.source_commit)))
if __name__=='__main__':main()
