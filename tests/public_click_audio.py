# SPDX-License-Identifier: MPL-2.0
"""Explicit public audio validation; never application input or training data.
Originals/conversions/results stay in build, never source distributions.
Run --download once to retrieve the four small hash-pinned public recordings.
"""
import argparse,hashlib,json,subprocess,urllib.request
from pathlib import Path
import numpy as np
import soundfile as sf
FIXTURES = [{'name': 'p', 'url': 'https://upload.wikimedia.org/wikipedia/commons/5/51/Voiceless_bilabial_plosive.ogg', 'page': 'https://commons.wikimedia.org/wiki/File:Voiceless_bilabial_plosive.ogg', 'author': 'Peter Isotalo', 'license': 'CC-BY-SA-3.0 / GFDL-1.2+', 'sha256': '576ae283995962f746a390fbc4461c01f6ce1e296c831d7beb9e1907acffa936', 'bytes': 20166, 'converted': 'WAV Float32 for testing only'}, {'name': 't', 'url': 'https://upload.wikimedia.org/wikipedia/commons/0/02/Voiceless_alveolar_plosive.ogg', 'page': 'https://commons.wikimedia.org/wiki/File:Voiceless_alveolar_plosive.ogg', 'author': 'Peter Isotalo', 'license': 'CC-BY-SA-3.0 / GFDL-1.2+', 'sha256': 'b67b7ae2c801ce37bc399c68473341ba6eba1e92e9a828a40583c46594f3d0c6', 'bytes': 27066, 'converted': 'WAV Float32 for testing only'}, {'name': 'k', 'url': 'https://upload.wikimedia.org/wikipedia/commons/e/e3/Voiceless_velar_plosive.ogg', 'page': 'https://commons.wikimedia.org/wiki/File:Voiceless_velar_plosive.ogg', 'author': 'Peter Isotalo', 'license': 'CC-BY-SA-3.0 / GFDL-1.2+', 'sha256': '5285ace1a53693315f32ad2c5faa9f87efe8690396685562877309a93dd9ecec', 'bytes': 25409, 'converted': 'WAV Float32 for testing only'}, {'name': 'mouth', 'url': 'https://cdn.freesound.org/previews/370/370869_3531538-hq.mp3', 'page': 'https://freesound.org/people/Xemptful/sounds/370869/', 'author': 'Xemptful', 'license': 'CC0-1.0', 'sha256': '2ea60081dcb07c2912c0972b2baa7d60a7e41ee223f5075f5622d2ab01d486fc', 'bytes': 31176, 'converted': 'MP3 public preview to WAV Float32 for testing only; no training'}]
p=argparse.ArgumentParser(description=__doc__);p.add_argument("--work",type=Path,required=True);p.add_argument("--cli",type=Path,required=True);p.add_argument("--download",action="store_true");a=p.parse_args()
root=a.work.resolve();root.mkdir(parents=True,exist_ok=True);cli=a.cli.resolve()
repo=Path(__file__).resolve().parents[1]
if not root.is_relative_to(repo/"build"):raise SystemExit("Fixture/output directory must be inside build")
def run(*args):
 r=subprocess.run([str(cli),*map(str,args)],capture_output=True,text=True,encoding="utf-8")
 if r.returncode:raise RuntimeError(r.stderr)
results=[]
for f in FIXTURES:
 name=f["name"];source=root/(name+(".mp3" if name=="mouth" else ".ogg"))
 if not source.exists():
  if not a.download:raise RuntimeError("Missing public fixture; explicitly pass --download")
  request=urllib.request.Request(f["url"],headers={"User-Agent":"NLBreathClick-test/1.0 (https://github.com/Nicotina4ever/NL-Breath-Click-AI)"})
  with urllib.request.urlopen(request,timeout=60) as response:data=response.read(1<<20)
  if hashlib.sha256(data).hexdigest()!=f["sha256"]:raise RuntimeError("Public fixture hash mismatch")
  source.write_bytes(data)
 assert hashlib.sha256(source.read_bytes()).hexdigest()==f["sha256"]
 x,sr=sf.read(source,dtype="float32");wav=root/(name+".wav");sf.write(wav,x,sr,subtype="FLOAT")
 before=hashlib.sha256(wav.read_bytes()).hexdigest();planpath=root/(name+".json")
 run("plan",wav,"--out",planpath,"--breaths","0","--clicks","1")
 plan=json.loads(planpath.read_text(encoding="utf-8"));events=plan["events"]
 output=root/(name+"_automatic");output.mkdir(exist_ok=True)
 run("process",wav,"--out-dir",output,"--breaths","0","--clicks","1")
 automatic,rate=sf.read(output/(name+"_breathclick.wav"),dtype="float32")
 assert automatic.shape==x.shape and rate==sr
 if name!="mouth":
  assert np.array_equal(automatic,x),("Legitimate real plosive changed",name,events)
 else:
  clusters=[]
  for event in events:
   t=event["start"]/sr
   if not clusters or t-clusters[-1]>.15:clusters.append(t)
  assert len(clusters)==3,("Creator-described three tongue clicks no longer form three candidate clusters",clusters)
  # Explicit algorithm evaluation; these are not verified human labels.
  for event in events:event.update(approved=True,enabled=True,decision_source="review_harness",training_eligible=False)
  planpath.write_text(json.dumps(plan),encoding="utf-8")
  audition=root/"mouth_review";audition.mkdir(exist_ok=True)
  run("edit",wav,"--plan",planpath,"--out-dir",audition)
  run("difference",wav,"--plan",planpath,"--out-dir",audition)
  edited,_=sf.read(audition/"mouth_edit.wav",dtype="float32")
  residual,_=sf.read(audition/"mouth_difference.wav",dtype="float32")
  assert edited.shape==x.shape and np.max(np.abs(residual-(x-edited)))<1e-7
 assert hashlib.sha256(wav.read_bytes()).hexdigest()==before
 results.append(dict(name=name,candidates=len(events),review_only=sum(e["classification"] in ("mouth_click","speech_overlap","vocal_transient","low_level_impulse") for e in events),automatic_changed_samples=int(np.count_nonzero(automatic!=x)),source=f))
# Known-reference mixtures use fragments of a real tongue click over a real
# vowel/attack. They support a numerical comparison, not a natural-speech claim.
mixtures=[];mixed=root/"mixed_voice";mixed.mkdir(exist_ok=True)
mouth,mouth_rate=sf.read(root/"mouth.wav",dtype="float32",always_2d=True)
for name,position,a0,b0 in (("t_vowel",.4,6528,6815),("k_vowel",.4,30077,30582),("t_attack",.96,50856,51073)):
 clean,rate=sf.read(root/(name[0]+".wav"),dtype="float32",always_2d=True)
 click=mouth[a0:b0].mean(axis=1)
 click=np.interp(np.arange(round(len(click)*rate/mouth_rate))*mouth_rate/rate,np.arange(len(click)),click)
 click*=.15/max(abs(click));first=round(position*rate);last=first+len(click)
 noisy=clean.copy();noisy[first:last]+=click[:,None]
 source=mixed/(name+".wav");sf.write(source,noisy,rate,subtype="FLOAT")
 source_sha=hashlib.sha256(source.read_bytes()).hexdigest()
 planpath=mixed/(name+".json");run("plan",source,"--out",planpath,"--breaths",0,"--clicks",1)
 plan=json.loads(planpath.read_text(encoding="utf-8"))
 plan["events"]=[dict(kind=1,start=first,end=last,enabled=True,approved=True,repair=True,mode="attenuate",repair_method="auto",fade_ms=0,decision_source="review_harness",training_eligible=False)]
 metrics={"untreated":float(np.mean((noisy[first:last]-clean[first:last])**2))}
 declined=False
 for method in ("linear","predictive","auto"):
  plan["events"][0]["repair_method"]=method;planpath.write_text(json.dumps(plan),encoding="utf-8")
  output=mixed/(name+"_"+method);output.mkdir(exist_ok=True)
  run("edit",source,"--plan",planpath,"--out-dir",output)
  run("difference",source,"--plan",planpath,"--out-dir",output)
  edited,_=sf.read(output/(name+"_edit.wav"),dtype="float32",always_2d=True)
  residual,_=sf.read(output/(name+"_difference.wav"),dtype="float32",always_2d=True)
  assert edited.shape==noisy.shape and np.array_equal(edited[:first],noisy[:first]) and np.array_equal(edited[last:],noisy[last:])
  assert np.max(np.abs(residual-(noisy-edited)))<1e-7
  assert hashlib.sha256(source.read_bytes()).hexdigest()==source_sha
  metrics[method]=float(np.mean((edited[first:last]-clean[first:last])**2))
  if method=="auto":
   render_report=json.loads((output/(name+"_edit.wav.edit.json")).read_text(encoding="utf-8"))
   declined=render_report["declined_events"]==1
   # These difficult voice mixtures previously became worse than untreated.
   assert declined and np.array_equal(edited,noisy),"Unsafe automatic reconstruction changed the real voice mixture"
 mixtures.append(dict(name=name,frames=last-first,sample_rate=rate,mse=metrics,auto_declined=declined))
report=dict(status="passed",limits="No frame-level human annotation, naturalness score or real saliva-over-speech accuracy claim. MP3 preview contains three tongue clicks, not a saliva training corpus. Mixtures are artificial overlays of two real recordings.",fixtures=results,mixtures=mixtures)
(root/"result.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
print(json.dumps(report))
