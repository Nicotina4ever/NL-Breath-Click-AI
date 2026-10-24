# SPDX-License-Identifier: MPL-2.0
"""Explicit release publisher. Uses configured Git credentials in memory only."""
import argparse
import hashlib
import json
import mimetypes
from pathlib import Path
import subprocess
import urllib.error
import urllib.parse
import urllib.request
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from distribution import REPOSITORY, TAG, VERSION

class GitHub:
    def __init__(self):
        reply=subprocess.check_output(['git','-c','credential.helper=','-c','credential.helper=manager','-c','credential.username=Nicotina4ever','-c','credential.interactive=never','credential','fill'],input=b'protocol=https\nhost=github.com\nusername=Nicotina4ever\n\n')
        credentials=dict(line.split('=',1) for line in reply.decode().splitlines() if '=' in line)
        self.headers={'Authorization':'Bearer '+credentials['password'],'Accept':'application/vnd.github+json','User-Agent':'NL-Breath-Click-release','X-GitHub-Api-Version':'2022-11-28'}
    def request(self,path,method='GET',value=None,data=None,content_type=None):
        url=path if path.startswith('https://') else 'https://api.github.com/repos/'+REPOSITORY+path
        if urllib.parse.urlparse(url).hostname not in ('api.github.com','uploads.github.com'):raise ValueError('Unexpected API host')
        headers=dict(self.headers)
        if value is not None:data=json.dumps(value).encode();headers['Content-Type']='application/json'
        if content_type:headers['Content-Type']=content_type
        request=urllib.request.Request(url,data=data,headers=headers,method=method)
        try:
            with urllib.request.urlopen(request,timeout=120) as response:
                body=response.read();return json.loads(body) if body else None
        except urllib.error.HTTPError as error:
            # Never log request headers, bearer credentials or response bodies.
            raise RuntimeError(f'GitHub HTTP {error.code}: {method} {urllib.parse.urlparse(url).path}') from None

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--notes',type=Path,required=True)
    parser.add_argument('--assets',type=Path,nargs='+',required=True)
    parser.add_argument('--publish',action='store_true',help='Explicitly create tag and publish the pre-release')
    args=parser.parse_args()
    if not args.publish:raise SystemExit('Publication requires --publish')
    if len(args.commit)!=40 or any(c not in '0123456789abcdef' for c in args.commit):raise SystemExit('Expected full public source commit')
    for path in args.assets:
        if not path.is_file():raise SystemExit('Missing asset: '+str(path))
    gh=GitHub();releases=gh.request('/releases')
    release=next((r for r in releases if r['tag_name']==TAG),None)
    tags=gh.request('/git/matching-refs/tags/'+TAG)
    if tags:
        if len(tags)!=1 or tags[0]['ref']!='refs/tags/'+TAG:raise SystemExit('Ambiguous release tag')
        target=tags[0]['object']
        while target['type']=='tag':target=gh.request('/git/tags/'+target['sha'])['object']
        if target['sha']!=args.commit:raise SystemExit('Existing tag points to another commit; refusing to change it')
    else:
        annotation=gh.request('/git/tags','POST',dict(tag=TAG,message='NL Breath Click Studio '+VERSION,object=args.commit,type='commit'))
        gh.request('/git/refs','POST',dict(ref='refs/tags/'+TAG,sha=annotation['sha']))
    if release and not release['draft']:raise SystemExit('Release is already published; no assets changed')
    if not release:release=gh.request('/releases','POST',dict(tag_name=TAG,target_commitish=args.commit,name='NL Breath Click Studio '+VERSION+' - Windows pre-release',body=args.notes.read_text(encoding='utf-8'),draft=True,prerelease=True,make_latest='false'))
    present={asset['name']:asset for asset in gh.request('/releases/'+str(release['id'])+'/assets')}
    for path in args.assets:
        payload=path.read_bytes();digest='sha256:'+hashlib.sha256(payload).hexdigest()
        if path.name in present:
            if present[path.name].get('digest')!=digest:raise SystemExit('Existing draft asset differs; no overwrite: '+path.name)
            continue
        url=release['upload_url'].split('{',1)[0]+'?name='+urllib.parse.quote(path.name)
        asset=gh.request(url,'POST',data=payload,content_type=mimetypes.guess_type(path.name)[0] or 'application/octet-stream')
        if asset['size']!=len(payload) or asset.get('digest')!=digest:raise SystemExit('Uploaded asset checksum mismatch: '+path.name)
        print('Verified upload:',path.name,digest)
    release=gh.request('/releases/'+str(release['id']),'PATCH',dict(draft=False,prerelease=True,make_latest='false'))
    print(json.dumps(dict(status='published',url=release['html_url'],tag=TAG,source_commit=args.commit,assets=[p.name for p in args.assets])))
if __name__=='__main__':main()
