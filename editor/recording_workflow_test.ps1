# SPDX-License-Identifier: MPL-2.0
# Optional full-recording GUI/CLI parity; never loads an original outside build.
param([Parameter(Mandatory=$true)][string]$AudioFixture,
      [Parameter(Mandatory=$true)][string]$WorkRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_window.ps1')
$repo=Split-Path -Parent $PSScriptRoot
$prefix=[IO.Path]::GetFullPath((Join-Path $repo 'build'))+'\'
$source=(Resolve-Path -LiteralPath $AudioFixture).Path
$work=[IO.Path]::GetFullPath($WorkRoot)
if(-not $source.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or
   -not $work.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Use build-local audio and evidence paths'}
if((Test-Path -LiteralPath $work) -and @(Get-ChildItem -LiteralPath $work -Force).Count){throw 'Use an empty evidence directory'}
New-Item -ItemType Directory -Path $work,(Join-Path $work 'profile') -Force|Out-Null
$env:APPDATA=Join-Path $work 'profile'
$bin=if($env:BC_TEST_BIN){$env:BC_TEST_BIN}else{Join-Path $repo 'build/bin/Release'}
function RecordingHash($path) {
 $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($path)
 try { return [BitConverter]::ToString($sha.ComputeHash($stream)) } finally { $stream.Dispose();$sha.Dispose() }
}
$hash=RecordingHash $source
Add-Type -ReferencedAssemblies System.Drawing @'
using System;using System.Drawing;using System.Runtime.InteropServices;
public static class RecordingGui {
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 struct Rect {public int a,b,c,d;}
 public static void Capture(IntPtr h,string path){Rect r;GetClientRect(h,out r);using(var bmp=new Bitmap(r.c,r.d))using(var g=Graphics.FromImage(bmp)){var dc=g.GetHdc();bool ok=PrintWindow(h,dc,1);g.ReleaseHdc(dc);if(!ok)throw new Exception("PrintWindow failed");bmp.Save(path);}}
}
'@
function Msg($handle,$message,$w=0,$l=0){return [long][RecordingGui]::SendMessageW($handle,$message,[IntPtr]$w,[IntPtr]$l)}
$rows=@();$process=Start-Process -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('"'+$source+'"') -PassThru -WindowStyle Normal
try {
 $window=Wait-BcEditorWindow -Process $process
 $breath=[RecordingGui]::GetDlgItem($window,2557);$click=[RecordingGui]::GetDlgItem($window,2558)
 $method=[RecordingGui]::GetDlgItem($window,2559)
 if((Msg $method 0x147) -ne 0){throw 'Normal fresh-profile default must use available combined voice analysis'}
 $probe=& (Join-Path $bin 'bc_studio_cli.exe') probe $source
 $match=[regex]::Match(($probe -join "`n"),'(?m)^frames=(\d+)')
 if(-not $match.Success){throw 'Source frame count unavailable'}
 $frames=[long]$match.Groups[1].Value
 foreach($mode in @(@('breaths',1,0),@('clicks',0,1),@('both',1,1))) {
  $path=Join-Path $work ($mode[0]+'.json')
  & (Join-Path $bin 'bc_studio_cli.exe') plan $source --detector fusion --breaths $mode[1] --clicks $mode[2] --out $path | Out-Null
  if($LASTEXITCODE -ne 0){throw 'Reference engine plan failed'}
  $expected=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
  if((Msg $breath 0xf0) -ne $mode[1]){[void](Msg $breath 0xf5)}
  if((Msg $click 0xf0) -ne $mode[2]){[void](Msg $click 0xf5)}
  [void](Msg $window 0x111 1202) # clear prior automatic proposals for independent category comparison
  [void](Msg $window 0x111 1200)
  $count=Msg $window 0x812d
  if($count -ne $expected.events.Count){throw "GUI/CLI candidate count differs for $($mode[0]): GUI=$count engine=$($expected.events.Count)"}
  for($i=0;$i -lt $count;$i++) {
   $e=$expected.events[$i]
   if((Msg $window 0x812e $i) -ne $e.start -or (Msg $window 0x812f $i) -ne $e.end -or (Msg $window 0x8145 $i) -ne $e.kind){throw "GUI/CLI bounds/category differ for $($mode[0]) event $i : GUI=$(Msg $window 0x812e $i)..$(Msg $window 0x812f $i) kind=$(Msg $window 0x8145 $i); CLI=$($e.start)..$($e.end) kind=$($e.kind)"}
  }
  [void](Msg $window 0x111 1208)
  if((Msg $window 0x8140) -ne 1 -or (Msg $window 0x8141) -ne $frames){throw 'Preview changed duration in attenuation/click mode'}
  $rows+=@{mode=$mode[0];candidates=$count;all_boundaries_and_categories_match_cli=$true;preview_duration_preserved=$true}
 }
 [RecordingGui]::Capture($window,(Join-Path $work 'studio.png'))
 if((RecordingHash $source) -ne $hash){throw 'Source changed'}
 @{status='passed';modes=$rows;source_unchanged=$true}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $work 'result.json') -Encoding UTF8
 Write-Host 'FULL RECORDING GUI PASS: three modes, all proposed bounds match the shared engine, preview duration and original preserved'
} finally {if($process -and -not $process.HasExited){Stop-Process -Id $process.Id -Force}}
