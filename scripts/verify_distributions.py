# SPDX-License-Identifier: MPL-2.0
"""Real clean-folder Windows package tests, without an installed other product."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import wave
import zipfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from distribution import VERSION, verify_archive

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--standalone',type=Path,required=True)
    parser.add_argument('--reaper',type=Path,required=True)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();work=args.work.resolve()
    if work.exists() and any(work.iterdir()):raise SystemExit('Use an empty verification directory')
    work.mkdir(parents=True,exist_ok=True)
    windows=ctypes.create_unicode_buffer(32768)
    if not ctypes.windll.kernel32.GetWindowsDirectoryW(windows,len(windows)):raise OSError('Cannot resolve Windows directory')
    env=os.environ.copy();env['SystemRoot']=windows.value;env['WINDIR']=windows.value;env['PATH']=os.path.join(windows.value,'System32')
    env['APPDATA']=str(work/'profile');env['LOCALAPPDATA']=str(work/'local');env['PYTHONDONTWRITEBYTECODE']='1'
    for key in list(env):
        if key.startswith(('BC_','NL_AUDIO_')):env.pop(key)
    results=[]
    for kind,path in [('standalone',args.standalone),('reaper',args.reaper)]:
        folder=work/kind;folder.mkdir()
        with zipfile.ZipFile(path) as archive:
            entries=verify_archive(archive);archive.extractall(folder)
        info=json.loads((folder/'BUILD_INFO.json').read_text(encoding='utf-8-sig'))
        assert info['source_commit']==args.commit and info['version']==VERSION
        assert not list(folder.rglob('reaper.exe'))
        if kind=='reaper':assert not list(folder.rglob('bc_studio.exe'))
        engine=folder/('bin/bc_studio_cli.exe' if kind=='reaper' else 'bc_studio_cli.exe')
        def call(*arguments,ok=True):
            result=subprocess.run([str(engine),*map(str,arguments)],cwd=folder,env=env,capture_output=True,timeout=90)
            assert (result.returncode==0)==ok,(kind,arguments,result.stderr.decode(errors='replace'))
            return result.stdout
        caps=json.loads(call('capabilities'))
        assert caps['source_commit']==args.commit and caps['protocol_version']==1 and caps['backend']=='cpu'
        call('selftest')
        fixture=folder/'synthetic.wav';rng=random.Random(72)
        with wave.open(str(fixture),'wb') as wav:
            wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(48000)
            wav.writeframes(b''.join(struct.pack('<h',rng.randint(-2500,2500)) for _ in range(144000)))
        original=hashlib.sha256(fixture.read_bytes()).hexdigest()
        for detector in ('dsp','fusion'):
            plan_path=folder/(detector+'.json')
            call('plan',fixture,'--out',plan_path,'--detector',detector)
            plan=json.loads(plan_path.read_text(encoding='utf-8-sig'))
            plan['events']=[dict(kind=0,start=48000,end=72000,mode='halve',enabled=True,approved=True,state='manual',manual_operation=True,decision_source='user',training_eligible=False,fade_ms=0,keep_fraction=.5)]
            plan_path.write_text(json.dumps(plan),encoding='utf-8')
            target=folder/('out_'+detector);target.mkdir()
            call('edit',fixture,'--plan',plan_path,'--out-dir',target)
            output=next(target.glob('*_edit.wav'))
            # Float WAV is parsed at the RIFF level (stdlib wave rejects float format).
            raw=output.read_bytes();pos=12;data_size=None;block=None
            while pos+8<=len(raw):
                tag,size=struct.unpack_from('<4sI',raw,pos)
                if tag==b'fmt ':block=struct.unpack_from('<H',raw,pos+8+12)[0]
                if tag==b'data':data_size=size
                pos+=8+size+(size&1)
            assert data_size//block==132000,(kind,detector,data_size,block)
        assert hashlib.sha256(fixture.read_bytes()).hexdigest()==original
        results.append(dict(product=kind,status='passed',files=len(entries),capabilities=caps,dsp_and_fusion=True,original_unchanged=True,following_duration_frames=132000))
    # ZIP installer and upgrades preserve any unmanaged user work.
    resource=work/'resource';resource.mkdir();(resource/'reaper.ini').write_text('[REAPER]\n',encoding='ascii')
    command=['powershell','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(work/'reaper/Install_REAPER.ps1'),'-ResourceDirectory',str(resource)]
    for _ in range(2):
        result=subprocess.run(command,capture_output=True,timeout=90)
        assert result.returncode==0,result.stderr.decode(errors='replace')
    installed=resource/'Scripts/NL Breath Click Studio'
    protected=installed/'user-note.txt';protected.write_text('User work must survive an update',encoding='utf-8')
    result=subprocess.run(command,capture_output=True,timeout=90)
    assert result.returncode!=0 and protected.read_text(encoding='utf-8')=='User work must survive an update'
    assert (installed/'bin/bc_studio_cli.exe').is_file()
    # First-use installer: a real verified backend, idempotence and tamper rejection.
    reapack=work/'reapack';reapack.mkdir()
    source=work/'reaper'
    shutil.copyfile(source/'bc_runtime.ps1',reapack/'bc_runtime.ps1')
    runtime=json.loads((args.reaper.parent/'runtime-release.json').read_text(encoding='utf-8'))
    (reapack/'runtime-release.json').write_text(json.dumps(runtime),encoding='utf-8')
    command=['powershell','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(reapack/'bc_runtime.ps1'),'-PackageDirectory',str(reapack),'-ArchivePath',str(args.reaper.resolve())]
    for _ in range(2):
        result=subprocess.run(command,capture_output=True,timeout=90)
        assert result.returncode==0,result.stderr.decode(errors='replace')
    ready=json.loads((reapack/'runtime'/VERSION/'READY.json').read_text(encoding='utf-8-sig'))
    assert ready['source_commit']==args.commit and not list(reapack.rglob('bc_studio.exe'))
    # A wrong expected archive hash must fail before an executable is installed.
    bad=work/'tampered';bad.mkdir();shutil.copyfile(source/'bc_runtime.ps1',bad/'bc_runtime.ps1')
    runtime['sha256']='0'*64;(bad/'runtime-release.json').write_text(json.dumps(runtime),encoding='utf-8')
    invalid=command.copy();invalid[invalid.index('-File')+1]=str(bad/'bc_runtime.ps1');invalid[invalid.index('-PackageDirectory')+1]=str(bad)
    result=subprocess.run(invalid,capture_output=True,timeout=90)
    assert result.returncode!=0 and not list(bad.rglob('*.exe'))
    report=dict(status='passed',source_commit=args.commit,products=results,first_use_offline_verified=True,idempotent_install=True,bad_sha_rejected=True,unmanaged_user_work_preserved=True,scope='Clean folders on existing Windows host; not a clean Windows VM')
    args.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
