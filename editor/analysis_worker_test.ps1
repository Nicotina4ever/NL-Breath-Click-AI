# SPDX-License-Identifier: MPL-2.0
# Real window/message processing, without moving the user's pointer or typing.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_window.ps1')
$repo=Split-Path -Parent $PSScriptRoot
$bin=if($env:BC_TEST_BIN){$env:BC_TEST_BIN}else{Join-Path $repo 'build/bin/Release'}
. (Join-Path $repo 'scripts/test_workspace.ps1')
$workspace=New-BcTestWorkspace 'analysis_worker';$work=$workspace.Path;$passed=$false
$env:APPDATA=Join-Path $work 'profile'
Add-Type @'
using System;using System.Text;using System.IO;using System.Runtime.InteropServices;
public static class AnalysisGui {
 delegate bool EnumProc(IntPtr h,IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr l);
 [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h,uint relation);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int count);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
 public static long Send(IntPtr h,uint m,long w=0,long l=0) {
   UIntPtr result;if(SendMessageTimeoutW(h,m,new IntPtr(w),new IntPtr(l),2,2000,out result)==IntPtr.Zero)throw new Exception("GUI stopped responding to message "+m);
   return unchecked((long)result.ToUInt64());
 }
 public static IntPtr Progress(IntPtr parent) {
   IntPtr found=IntPtr.Zero;EnumWindows((h,l)=>{var s=new StringBuilder(256);GetClassName(h,s,256);
     if(s.ToString()=="BreathClickAnalysisProgress" && GetWindow(h,4)==parent){found=h;return false;}return true;
   },IntPtr.Zero);return found;
 }
 public static void Fixture(string path) {
   int frames=16000*90;
   using(var writer=new BinaryWriter(File.Create(path))) {
     writer.Write(Encoding.ASCII.GetBytes("RIFF"));writer.Write(36+frames*4);writer.Write(Encoding.ASCII.GetBytes("WAVEfmt "));
     writer.Write(16);writer.Write((short)3);writer.Write((short)1);writer.Write(16000);writer.Write(64000);writer.Write((short)4);writer.Write((short)32);
     writer.Write(Encoding.ASCII.GetBytes("data"));writer.Write(frames*4);
     for(int i=0;i<frames;i++)writer.Write((float)(.03*Math.Sin(i*.071)+.01*Math.Sin(i*.913)));
   }
 }
}
'@
function Msg($h,$m,$w=0,$l=0){[AnalysisGui]::Send($h,$m,$w,$l)}
function Check($ok,$text){if(-not $ok){throw $text};Write-Host "ANALYSIS GUI OK: $text"}
function Hash($path){
 $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($path)
 try{return [BitConverter]::ToString($sha.ComputeHash($stream))}finally{$stream.Dispose();$sha.Dispose()}
}
function StartAnalysis($window){
 Check ([AnalysisGui]::PostMessageW($window,0x111,[IntPtr]1200,[IntPtr]0)) 'analysis command posted'
 $clock=[Diagnostics.Stopwatch]::StartNew()
 do {
  $dialog=[AnalysisGui]::Progress($window)
  if($dialog -ne [IntPtr]::Zero){return $dialog}
  Start-Sleep -Milliseconds 20
 }while($clock.Elapsed.TotalSeconds -lt 10)
 throw 'Analysis progress did not appear'
}
function WaitClosed($dialog,$process){
 $clock=[Diagnostics.Stopwatch]::StartNew()
 while([AnalysisGui]::IsWindow($dialog) -and $clock.Elapsed.TotalSeconds -lt 30){Start-Sleep -Milliseconds 20}
 Check (-not [AnalysisGui]::IsWindow($dialog)) 'progress closes within timeout'
 Check (-not $process.HasExited) 'editor remains alive'
}
$wav=Join-Path $work 'synthetic.wav';[AnalysisGui]::Fixture($wav)
$hash=Hash $wav
$session=Join-Path $work 'input.bcnstudio.json'
@{schema=2;source=$wav;regions=@(@{kind=0;start=16000;end=20000;enabled=$true;approved=$true;from_detection=$false;decision_source='user';learning_label='breath_confirmed';training_eligible=$true});events=@();corrections=@()}|
 ConvertTo-Json -Depth 6|Set-Content -LiteralPath $session -Encoding UTF8
$process=Start-Process -WindowStyle Normal -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session',('"'+$session+'"')) -PassThru
try {
 $window=Wait-BcEditorWindow -Process $process
 $ready=[Diagnostics.Stopwatch]::StartNew()
 while((Msg $window 0x812d) -ne 1 -and $ready.Elapsed.TotalSeconds -lt 10){Start-Sleep -Milliseconds 20}
 $count=Msg $window 0x812d;$history=Msg $window 0x8133
 Check ($count -eq 1) "manual classified region loaded (count=$count)"
 $dialog=StartAnalysis $window
 Check (-not [AnalysisGui]::IsWindowEnabled($window)) 'owner protected while analysis runs'
 Check ((Msg $window 0x812d) -eq $count) 'read-only query responds while worker runs'
 [void](Msg $window 0x111 1202)
 Check ((Msg $window 0x812d) -eq $count) 'nested editing rejected while worker runs'
 [void](Msg ([AnalysisGui]::GetDlgItem($dialog,2)) 0xf5)
 WaitClosed $dialog $process
 Check ([AnalysisGui]::IsWindowEnabled($window)) 'owner enabled after cancellation'
 Check ((Msg $window 0x812d) -eq $count -and (Msg $window 0x8133) -eq $history) 'cancel preserves regions and Undo history'
 Check ((Msg $window 0x812e 0) -eq 16000 -and (Msg $window 0x812f 0) -eq 20000 -and (Msg $window 0x8131 0) -eq 1) 'manual bounds and approval remain intact'
 # Retry and complete through the same worker, using explicit acoustic mode.
 $method=[AnalysisGui]::GetDlgItem($window,2559)
 [void](Msg $method 0x14e 2)
 [void](Msg $window 0x111 (2559 -bor (1 -shl 16)) $method.ToInt64())
 Check ((Msg $method 0x147) -eq 2) 'retry uses explicit acoustic analysis'
 $dialog=StartAnalysis $window
 WaitClosed $dialog $process
 Check ((Msg $window 0x812d) -ge 1 -and [AnalysisGui]::IsWindowEnabled($window)) 'completed retry keeps manual review and restores controls'
 # Closing the app during AI analysis cancels/join workers before recovery/exit.
 [void](Msg $method 0x14e 0)
 [void](Msg $window 0x111 (2559 -bor (1 -shl 16)) $method.ToInt64())
 $dialog=StartAnalysis $window
 [void][AnalysisGui]::PostMessageW($window,0x10,[IntPtr]0,[IntPtr]0)
 Check ($process.WaitForExit(20000)) 'close during analysis exits after worker joins'
 Check ($process.ExitCode -eq 0) 'normal exit after cancellation'
 Check ((Hash $wav) -eq $hash) 'source PCM remains unchanged'
 Write-Host 'ANALYSIS WORKER GUI PASS: responsive UI, cancellation, preserved edits/history, retry and close'
 $passed=$true
}finally{
 if($process -and -not $process.HasExited){Stop-Process -Id $process.Id -Force;[void]$process.WaitForExit(5000)}
 Close-BcTestWorkspace $workspace $passed
}
