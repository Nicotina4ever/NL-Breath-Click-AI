# SPDX-License-Identifier: MPL-2.0
# Actual controls and WaveOut audition; no physical mouse/keyboard injection.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_window.ps1')
$repo=Split-Path -Parent $PSScriptRoot
$bin=if($env:BC_TEST_BIN){$env:BC_TEST_BIN}else{Join-Path $repo 'build/bin/Release'}
. (Join-Path $repo 'scripts/test_workspace.ps1')
$workspace=New-BcTestWorkspace 'review';$work=$workspace.Path;$passed=$false
$env:APPDATA=Join-Path $work 'profile';$env:TEMP=$work;$env:TMP=$work
Add-Type -ReferencedAssemblies System.Drawing @'
using System;using System.IO;using System.Text;using System.Drawing;using System.Runtime.InteropServices;
public static class ReviewGui {
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 public struct Rect {public int l,t,r,b;}
 public static void Capture(IntPtr h,string path){Rect r;GetClientRect(h,out r);using(var bmp=new Bitmap(r.r,r.b))using(var g=Graphics.FromImage(bmp)){var dc=g.GetHdc();bool ok=PrintWindow(h,dc,1);g.ReleaseHdc(dc);if(!ok)throw new Exception("PrintWindow failed");bmp.Save(path);}}
 public static void Fixture(string path){int n=48000*12;using(var w=new BinaryWriter(File.Create(path))){w.Write(Encoding.ASCII.GetBytes("RIFF"));w.Write(36+n*4);w.Write(Encoding.ASCII.GetBytes("WAVEfmt "));w.Write(16);w.Write((short)3);w.Write((short)1);w.Write(48000);w.Write(192000);w.Write((short)4);w.Write((short)32);w.Write(Encoding.ASCII.GetBytes("data"));w.Write(n*4);for(int i=0;i<n;i++)w.Write(i==48000?.001f:0f);}}
}
'@
function Msg($h,$m,$w=0,$l=0){return [long][ReviewGui]::SendMessageW($h,$m,[IntPtr]$w,[IntPtr]$l)}
function Cmd($id){[void](Msg $script:h 0x111 $id)}
function Check($ok,$text){if(-not $ok){throw $text};Write-Host "REVIEW OK: $text"}
function SetReviewFilter($index){[void](Msg $script:filter 0x14e $index);[void](Msg $script:h 0x111 (2511 -bor (1 -shl 16)) $script:filter.ToInt64())}
function Finished($end){
 $clock=[Diagnostics.Stopwatch]::StartNew()
 while((Msg $script:h 0x8146) -eq 1 -and $clock.Elapsed.TotalSeconds -lt 3){Start-Sleep -Milliseconds 15}
 Check ((Msg $script:h 0x8146) -eq 0) 'audition stops without playing the rest of the take'
 Check ((Msg $script:h 0x8147) -eq $end) "audition endpoint follows the selected signal ($end)"
}
$wav=Join-Path $work 'synthetic.wav';[ReviewGui]::Fixture($wav)
$regions=@(
 @{kind=1;start=48000;end=48005;classification='digital_click';approved=$true},
 @{kind=0;start=480000;end=490000;classification='ai';approved=$true},
 @{kind=1;start=192000;end=192144;classification='speech_overlap';approved=$false},
 @{kind=1;start=384000;end=384050;classification='digital_click';approved=$false;decision_source='user';enabled=$false;learning_label='click_false_positive';training_eligible=$true},
 @{kind=1;start=336000;end=336050;classification='saliva_click';approved=$true;repair_feedback='unsatisfactory'},
 @{kind=1;start=288000;end=288144;classification='isolated_click_review';approved=$false},
 @{kind=0;start=96000;end=120000;classification='manual';approved=$true;manual_operation=$true;edit_mode=2;decision_source='user'}
)
foreach($r in $regions){if(-not $r.ContainsKey('enabled')){$r.enabled=$true};$r.from_detection=-not $r.manual_operation;$r.repair=$r.kind -eq 1;$r.fade_ms=0;$r.keep_fraction=.5}
$session=Join-Path $work 'input.bcnstudio.json'
@{schema=2;source=$wav;regions=$regions;events=@();corrections=@();crossfade_ms=0}|ConvertTo-Json -Depth 7|Set-Content -Encoding UTF8 -LiteralPath $session
$process=Start-Process -WindowStyle Normal -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session',('"'+$session+'"')) -PassThru
try {
 $script:h=Wait-BcEditorWindow -Process $process
 $clock=[Diagnostics.Stopwatch]::StartNew();while((Msg $h 0x812d) -ne 7 -and $clock.Elapsed.TotalSeconds -lt 10){Start-Sleep -Milliseconds 20}
 Check ((Msg $h 0x812d) -eq 7) 'unsorted edited regions loaded'
 $panel=[ReviewGui]::GetDlgItem($h,3201);$script:filter=[ReviewGui]::GetDlgItem($panel,2511)
 Cmd 1320;Check ((Msg $h 0x8132) -eq 0) 'next starts at the first chronological event'
 Check ((Msg $h 0x8135) -le 4000 -and (Msg $h 0x8134) -le 48000 -and ((Msg $h 0x8134)+(Msg $h 0x8135)) -gt 48005) 'navigation frames a short click at an editable zoom without manual zooming'
 Cmd 1320;Check ((Msg $h 0x8132) -eq 6) 'next follows edited time rather than vector insertion order'
 SetReviewFilter 10;Cmd 1320;Check ((Msg $h 0x8132) -eq 2) 'undecided filter skips approved events and verified exclusions'
 Cmd 1313;Cmd 1320;Check ((Msg $h 0x8132) -eq 5) 'next works after the current decision removes it from the filter'
 Check ([ReviewGui]::IsWindowVisible([ReviewGui]::GetDlgItem($panel,2545))) 'click strategies are visible directly in the panel'
 Check (-not [ReviewGui]::IsWindowVisible([ReviewGui]::GetDlgItem($panel,1317))) 'breath shortening buttons do not cover click repair controls'
 Cmd 2546;Check ((Msg $h 0x8131 5) -eq 1) 'visible Linear command includes only the selected click'
 Cmd 1100;Check ((Msg $h 0x8131 5) -eq 0) 'one Undo restores the proposal before method choice'
 Cmd 2550;SetReviewFilter 12;Cmd 1320;Check ((Msg $h 0x8132) -eq 5) 'repair feedback has its own review filter'
 SetReviewFilter 11;Cmd 1320;Cmd 1320;Cmd 1320
 Check ((Msg $h 0x8132) -eq 4) 'isolated click navigation reaches the selected event at seven seconds'
 Cmd 1319;$end=345650;Finished $end
 Check ((Msg $h 0x8136) -eq 326400 -and (Msg $h 0x8137) -eq $end) 'audition includes 200 ms context without changing event boundaries'
 Check ((Msg $h 0x812e 4) -eq 336000 -and (Msg $h 0x812f 4) -eq 336050) 'listening does not change the actual repair interval'
 Check ((Msg $h 0x8134) -eq 334560) 'context audition keeps the selected click in view while context plays outside the zoom'
 Cmd 2561;Finished ($end-12000)
 Check ([BcTestWindow]::Text([ReviewGui]::GetDlgItem($panel,3111)) -match 'Sicurezza [1-9]') 'overview distinguishes safety refusals from successful applications'
 Check ((Msg $h 0x8141) -eq (576000-12000)) 'processed audition uses the cumulative TimeMap'
 Cmd 2562;Finished $end
 [ReviewGui]::Capture($h,(Join-Path $work 'review.png'))
 [void](Msg $h 0x115 7 $panel.ToInt64()) # SB_BOTTOM
 $panelRect=New-Object ReviewGui+Rect;$labelRect=New-Object ReviewGui+Rect
 [void][ReviewGui]::GetWindowRect($panel,[ref]$panelRect)
 [void][ReviewGui]::GetWindowRect([ReviewGui]::GetDlgItem($panel,3110),[ref]$labelRect)
 Check ($labelRect.t -ge $panelRect.t -and $labelRect.b -le $panelRect.b) 'scrollbar makes the lower controls reachable in the same panel'
 Cmd 2541
 [void][ReviewGui]::PostMessageW($h,0x10,[IntPtr]0,[IntPtr]0)
 Check ($process.WaitForExit(5000)) 'review session closes and saves recovery'
 $project=Get-Content -LiteralPath (Join-Path $work 'BreathClick/recovery.bcnproj.json') -Raw|ConvertFrom-Json
 $saved=Get-Content -LiteralPath (Join-Path (Join-Path $work 'BreathClick') $project.files[0].session_relative) -Raw|ConvertFrom-Json
 Check ($saved.regions[4].learning_label -eq 'click_false_positive' -and $saved.regions[4].training_eligible) 'explicit false-click label persists independently of editorial approval'
 Check ($saved.regions[5].repair_feedback -eq 'unsatisfactory' -and -not $saved.regions[5].training_eligible) 'repair feedback persists without inventing a training label'
 $recoverySession=Join-Path (Join-Path $work 'BreathClick') $project.files[0].session_relative
 $exportRoot=Join-Path $work 'exported'
 $export=Start-Process -WindowStyle Hidden -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session-export',('"'+$recoverySession+'"'),('"'+$exportRoot+'"')) -PassThru -Wait
 Check ($export.ExitCode -eq 0) 'reviewed session exports through the same engine'
 $exportedPlan=Get-Content -LiteralPath (Join-Path $exportRoot 'synthetic_breathclick.wav.plan.json') -Raw|ConvertFrom-Json
 Check ($exportedPlan.events.Count -eq 7 -and $exportedPlan.events[4].learning_label -eq 'click_false_positive' -and -not $exportedPlan.events[4].approved) 'exported plan preserves verified negatives without applying them'
 Check ($exportedPlan.events[5].repair_feedback -eq 'unsatisfactory' -and -not $exportedPlan.events[5].training_eligible) 'exported plan preserves unapproved repair feedback for other hosts'
 Write-Host "REVIEW WORKFLOW PASS; evidence=$work"
 $passed=$true
}finally{
 if($process -and -not $process.HasExited){Stop-Process -Id $process.Id -Force;[void]$process.WaitForExit(5000)}
 Close-BcTestWorkspace $workspace $passed
}
