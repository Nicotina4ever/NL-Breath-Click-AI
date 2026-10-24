# SPDX-License-Identifier: MPL-2.0
"""Generate ReaPack metadata from immutable source and verified release assets."""
import argparse
import datetime
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from distribution import REAPER_FILES, REPOSITORY, TAG, VERSION, digest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--runtime-manifest',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=Path('index.xml'))
    args=parser.parse_args()
    def blob(name):return subprocess.check_output(['git','show',args.commit+':'+name])
    index=ET.Element('index',version='1',name='NL Audio Tools')
    category=ET.SubElement(index,'category',name='NL Audio Tools')
    package=ET.SubElement(category,'reapack',name='NL Breath Click Studio.lua',type='script',desc='NL Breath Click Studio - REAPER Windows Beta')
    version=ET.SubElement(package,'version',name=VERSION,author='Nicola Lanci Voice Over',time=datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))
    ET.SubElement(version,'changelog').text='First independent Windows beta. Verified backend installed on first use; no standalone or REAPER copy required.'
    for name in REAPER_FILES:
        attrs={'file':'NL Breath Click Studio/'+name,'platform':'win64','hash':'1220'+digest(blob('reaper/'+name))}
        if name=='BreathClickStudio.lua':attrs['main']='main'
        ET.SubElement(version,'source',attrs).text=f'https://raw.githubusercontent.com/{REPOSITORY}/{args.commit}/reaper/{name}'
    attrs={'file':'NL Breath Click Studio/runtime-release.json','platform':'win64','hash':'1220'+digest(args.runtime_manifest.read_bytes())}
    ET.SubElement(version,'source',attrs).text=f'https://github.com/{REPOSITORY}/releases/download/{TAG}/runtime-release.json'
    # MPL and third-party notices travel with the backend; source attribution is available before installation.
    for name in ('LICENSE','THIRD_PARTY_NOTICES.md'):
        ET.SubElement(version,'source',file='NL Breath Click Studio/'+name,platform='win64',hash='1220'+digest(blob(name))).text=f'https://raw.githubusercontent.com/{REPOSITORY}/{args.commit}/{name}'
    ET.indent(index)
    args.output.write_bytes(ET.tostring(index,encoding='utf-8',xml_declaration=True)+b'\n')
    print('ReaPack index generated:',args.output)
if __name__=='__main__':main()
