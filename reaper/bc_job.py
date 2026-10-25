# SPDX-License-Identifier: MPL-2.0
"""Portable JSON-v1 job runner. Python 3 standard library only, no shell execution.
Windows releases retain the PowerShell worker; POSIX source integration uses this.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

class Cancelled(Exception):
    pass

def strict_object(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('Duplicate JSON key: '+key)
        result[key]=value
    return result

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'),object_pairs_hook=strict_object,
                      parse_constant=lambda value:(_ for _ in ()).throw(ValueError('Invalid JSON number')))

def execute(request):
    root=request.resolve().parent;result_path=root/'result.json'
    if result_path.exists():raise RuntimeError('Result already exists; use a new job directory')
    lock=root/'.worker.lock'
    with lock.open('x',encoding='ascii') as file:file.write(str(os.getpid()))
    try:
        result={'status':'failed','error':'Worker did not complete'}
        try:
            job=read(request)
            schema=job.get('schema',1)
            if type(schema) is not int or schema!=1:raise ValueError('Unsupported job schema')
            exe=Path(job['exe']).resolve()
            if exe.name not in ('bc_studio_cli','bc_studio_cli.exe') or not exe.is_file():raise ValueError('Select the current bc_studio_cli engine')
            def run(*arguments):
                if (root/'cancel').exists():raise Cancelled()
                env=os.environ.copy()
                flags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0
                child=subprocess.Popen([str(exe),*map(str,arguments)],cwd=exe.parent,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,creationflags=flags)
                started=time.monotonic()
                try:
                    while True:
                        if (root/'cancel').exists():raise Cancelled()
                        if time.monotonic()-started>900:raise TimeoutError('Engine timed out')
                        try:out,err=child.communicate(timeout=.05);break
                        except subprocess.TimeoutExpired:pass
                    with (root/'engine.log').open('ab') as log:log.write(out+err)
                    if child.returncode:raise RuntimeError('Engine exit '+str(child.returncode)+': '+(out+err).decode('utf-8',errors='replace'))
                    return out
                finally:
                    if child.poll() is None:child.kill();child.communicate()
            caps=json.loads(run('capabilities'))
            if caps.get('reaper_bridge')!=1 or (job.get('direct') and caps.get('direct_editing')!=2):raise ValueError('Incompatible engine protocol')
            mode=job['mode']
            if mode=='analyze':
                segment=Path(job['prepared']) if job.get('prepared') else root/'input.wav'
                if job.get('prepared'):run('verify-source',job['source'],'--sha256',job['source_sha256'])
                else:run('slice',job['source'],'--out',segment,'--start',job['offset'],'--duration',job['duration'])
                plan=root/'analysis.json'
                args=['plan',segment,'--out',plan,'--detector',job['detector'],'--reduction-db',job['reduction_db'],'--fade-ms',job['fade_ms']]
                args.extend(('--breaths','1' if job.get('detect_breath',True) else '0','--clicks','1' if job.get('detect_clicks',False) else '0','--click-sensitivity',job.get('click_sensitivity',.5)))
                run(*args)
                if job.get('profile'):
                    filtered=root/'personal.json';run('profile-filter','--plan',plan,'--profile',job['profile'],'--out',filtered);plan=filtered
                meta=read(Path(str(segment)+'.source.json'))
                run('verify-source',job['source'],'--sha256',meta['source_sha256'])
                result=dict(status='complete',plan=str(plan),prepared=str(segment),source_sha256=meta['source_sha256'],frames=meta['frames'])
            elif mode in ('render','difference'):
                run('verify-source',job['source'],'--sha256',job['source_sha256'])
                run('difference' if mode=='difference' else 'edit',job['prepared'],'--plan',job['plan'],'--out-dir',root)
                run('verify-source',job['source'],'--sha256',job['source_sha256'])
                output=root/(Path(job['prepared']).stem+('_difference.wav' if mode=='difference' else '_edit.wav'));report=Path(str(output)+'.edit.json')
                if not report.is_file():raise RuntimeError('Render report missing')
                result=dict(status='complete',output=str(output),report=str(report))
            elif mode in ('profile-train','labels-export'):
                arguments=[mode,'--out',job['output']]
                for plan in job['plans']:arguments.extend(('--plan',plan))
                run(*arguments);result=dict(status='complete',output=job['output'])
            else:raise ValueError('Unknown job mode')
        except Cancelled:result=dict(status='cancelled',error='CANCELLED')
        except Exception as error:result=dict(status='failed',error=str(error))
        pending=root/'result.pending'
        with pending.open('x',encoding='utf-8') as file:json.dump(result,file,ensure_ascii=False)
        os.replace(pending,result_path)
        return result
    finally:lock.unlink(missing_ok=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--request',type=Path,required=True)
    print(json.dumps(execute(parser.parse_args().request),ensure_ascii=True))
