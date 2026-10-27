# SPDX-License-Identifier: MPL-2.0
# Real Win32 controls, mouse capture, painted pixels and persisted editor state.
param([string]$AudioFixture='')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'test_window.ps1')
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
public class Rc2Gui {
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint t,ref Info i);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h,uint command);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)] public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll")] public static extern bool EnumThreadWindows(uint id,EnumProc p,IntPtr l);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,System.Text.StringBuilder s,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h,System.Text.StringBuilder s,int n);
 public static string Fields(IntPtr dialog) {
   string result="";
   for(IntPtr h=GetWindow(dialog,5);h!=IntPtr.Zero;h=GetWindow(h,2)) {
     var s=new System.Text.StringBuilder(512); GetWindowTextW(h,s,512); result+="["+s+"] ";
   } return result;
 }
 public static string Text(IntPtr h) {
   var s=new System.Text.StringBuilder(512); GetWindowTextW(h,s,512);return s.ToString();
 }
 public static void OutcomeFixture(string path) {
   const int sr=48000,n=sr*4;
   var rng=new Random(553);
   using(var w=new BinaryWriter(File.Create(path))) {
     w.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));w.Write(36+n*4);
     w.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt "));w.Write(16);
     w.Write((short)3);w.Write((short)1);w.Write(sr);w.Write(sr*4);w.Write((short)4);w.Write((short)32);
     w.Write(System.Text.Encoding.ASCII.GetBytes("data"));w.Write(n*4);
     for(int i=0;i<n;i++) {
       float x=i<sr*3/2 ? .02f : (float)(.04*Math.Sin(2*Math.PI*173*i/sr)+(rng.NextDouble()-.5)*.06);
       if(i==sr)x+=.6f;
       w.Write(x);
     }
   }
 }
 public static string Windows(IntPtr parent) {
   string result="";
   EnumThreadWindows(GetWindowThreadProcessId(parent,IntPtr.Zero),(h,l)=>{
     var s=new System.Text.StringBuilder(256); GetClassNameW(h,s,256);
     result+=h+":"+s+" owner="+GetWindow(h,4)+"; "; return true;
   },IntPtr.Zero); return result;
 }
 public static IntPtr Modal(IntPtr parent,string cls) {
   IntPtr result=IntPtr.Zero;
   EnumThreadWindows(GetWindowThreadProcessId(parent,IntPtr.Zero),(h,l)=>{
     var s=new System.Text.StringBuilder(256); GetClassNameW(h,s,256);
     if(s.ToString()==cls && GetWindow(h,4)==parent){result=h;return false;} return true;
   },IntPtr.Zero); return result;
 }
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int l,t,r,b; }
 [StructLayout(LayoutKind.Sequential)] public struct Info { public int size,flags; public IntPtr active,focus,capture,menu,move,caret; public Rect rect; }
 public static bool Captured(IntPtr h) { Info i=new Info(); i.size=Marshal.SizeOf(i); return GetGUIThreadInfo(GetWindowThreadProcessId(h,IntPtr.Zero),ref i) && i.capture==h; }
 public static long WavFrames(string path) {
   using(BinaryReader r=new BinaryReader(File.OpenRead(path))) {
     r.ReadBytes(12); int align=0;
     while(r.BaseStream.Position+8<=r.BaseStream.Length) {
       string id=new string(r.ReadChars(4)); uint size=r.ReadUInt32(); long next=r.BaseStream.Position+size+(size%2);
       if(id=="fmt ") { byte[] f=r.ReadBytes((int)size); align=BitConverter.ToUInt16(f,12); }
       if(id=="data" && align>0)return size/align;
       r.BaseStream.Position=next;
     } throw new Exception("WAV frame count missing");
   }
 }
 public static double WavSample(string path,long frame) {
   using(BinaryReader r=new BinaryReader(File.OpenRead(path))) {
     r.ReadBytes(12); int fmt=0,bits=0,align=0;
     while(r.BaseStream.Position+8<=r.BaseStream.Length) {
       string id=new string(r.ReadChars(4)); uint size=r.ReadUInt32(); long next=r.BaseStream.Position+size+(size%2);
       if(id=="fmt ") { byte[] f=r.ReadBytes((int)size); fmt=BitConverter.ToUInt16(f,0); align=BitConverter.ToUInt16(f,12); bits=BitConverter.ToUInt16(f,14); if(fmt==65534)fmt=BitConverter.ToUInt16(f,24); }
       if(id=="data") {
         r.BaseStream.Position+=frame*align;
         if(fmt==3 && bits==32)return r.ReadSingle();
         if(fmt==1 && bits==16)return r.ReadInt16()/32768.0;
         if(fmt==1 && bits==24) { int v=r.ReadByte() | (r.ReadByte()<<8) | (r.ReadByte()<<16); if((v&0x800000)!=0)v|=unchecked((int)0xff000000);return v/8388608.0; }
         if(fmt==1 && bits==32)return r.ReadInt32()/2147483648.0;
         throw new Exception("Unsupported fixture WAV format");
       }
       r.BaseStream.Position=next;
     }
     throw new Exception("WAV data missing");
   }
 }
 public static int Pixel(IntPtr h,int x,int y,string path) {
   Rect r; GetClientRect(h,out r);
   using(Bitmap b=new Bitmap(r.r,r.b)) using(Graphics g=Graphics.FromImage(b)) {
     IntPtr dc=g.GetHdc(); bool ok=PrintWindow(h,dc,0); g.ReleaseHdc(dc);
     if(!ok) throw new Exception("PrintWindow failed");
     if(!String.IsNullOrEmpty(path)) b.Save(path);
     return b.GetPixel(x,y).ToArgb() & 0xffffff;
   }
 }
}
"@
$root=Split-Path -Parent $PSScriptRoot
$bin=if($env:BC_TEST_BIN){$env:BC_TEST_BIN}else{Join-Path $root 'build\bin\Release'}
. (Join-Path $root 'scripts/test_workspace.ps1')
$workspace=New-BcTestWorkspace 'rc2_gui';$work=$workspace.Path;$passed=$false
$env:APPDATA=Join-Path $work 'profile'
New-Item -ItemType Directory -Path $env:APPDATA | Out-Null
$wav=Join-Path $work 'fixture.wav'
if ($AudioFixture) {
 $fixturePath=(Resolve-Path -LiteralPath $AudioFixture).Path
 $buildPrefix=[IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
 if(-not $fixturePath.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Only build-local copies are allowed'}
 Copy-Item -LiteralPath $fixturePath -Destination $wav
} else {
 & (Join-Path $bin 'bc_cli.exe') gen-test $wav --quiet | Out-Null
 if($LASTEXITCODE -ne 0){throw 'fixture failed'}
}
function Hash($path) {
 $sha=[Security.Cryptography.SHA256]::Create(); $stream=[IO.File]::OpenRead($path)
 try { return [BitConverter]::ToString($sha.ComputeHash($stream)) } finally { $stream.Dispose(); $sha.Dispose() }
}
$original=Hash $wav
$regions=@()
foreach($start in @(144000,192000,240000)) {
 $click=$start -eq 240000
 $regions+=@{kind=[int]$click;start=$start;end=if($click){240120}else{$start+24000}; enabled=$true;approved=$true;from_detection=$true;
 reduction_db=-12;edit_mode=0;fade_ms=2;classification='dsp';decision_source='automatic';training_eligible=$false}
}
$session=Join-Path $work 'input.bcnstudio.json'
@{schema=2;source=$wav;regions=$regions;events=@();corrections=@()} | ConvertTo-Json -Depth 6 | Set-Content $session -Encoding UTF8
function Msg($h,$m,$w=0,$l=0) { return [int64][Rc2Gui]::SendMessageW($h,$m,[IntPtr]$w,[IntPtr]$l) }
function Cmd($id) { [void](Msg $script:h 0x111 $id); Start-Sleep -Milliseconds 70 }
function Check($yes,$message) { if(-not $yes){throw $message}; Write-Host "RC2 OK: $message" }
function MouseParam($x,$y=100) { return ($x -band 65535) -bor (($y -band 65535) -shl 16) }
function X($frame) { return [int](Msg $script:view 0x8065 $frame) }
function ClickFrame($frame,$modifiers=1) { $lp=MouseParam (X $frame); [void](Msg $script:view 0x201 $modifiers $lp); [void](Msg $script:view 0x202 ($modifiers -band 12) $lp) }
function Drag($a,$b,$release=$true) {
 [void](Msg $script:view 0x201 1 (MouseParam (X $a)))
 [void](Msg $script:view 0x200 1 (MouseParam (X $b)))
 if($release){[void](Msg $script:view 0x202 0 (MouseParam (X $b)))}
}
function OpenModal($command,$class='BreathClickAttenuationDialog') {
 if(-not [Rc2Gui]::PostMessageW($script:h,0x111,[IntPtr]$command,[IntPtr]0)){throw 'PostMessage command failed'}
 for($i=0;$i -lt 40;$i++) {
   Start-Sleep -Milliseconds 50
   $dialog=[Rc2Gui]::Modal($script:h,$class)
   if($dialog -ne [IntPtr]::Zero){return $dialog}
 }
 throw "Modal dialog did not open for command $command; windows=$([Rc2Gui]::Windows($script:h)); selection=$(Msg $script:h 0x8136)..$(Msg $script:h 0x8137)"
}
function SetField($dialog,$id,$value) {
 $field=[Rc2Gui]::GetDlgItem($dialog,$id)
 if($field -eq [IntPtr]::Zero){throw "Dialog field $id missing"}
 [void][Rc2Gui]::SendText($field,0x000c,[IntPtr]0,[string]$value)
}
function Key($dialog,$key) {
 if(-not [Rc2Gui]::PostMessageW($dialog,0x100,[IntPtr]$key,[IntPtr]0)){throw 'Keyboard PostMessage failed'}
 [void][Rc2Gui]::PostMessageW($dialog,0x101,[IntPtr]$key,[IntPtr]0)
 Start-Sleep -Milliseconds 100
}
function Closed($dialog) {
 for($i=0;$i -lt 30 -and [Rc2Gui]::IsWindow($dialog);$i++){Start-Sleep -Milliseconds 50}
 if([Rc2Gui]::IsWindow($dialog)){throw "Modal stayed open: $([Rc2Gui]::Fields($dialog))"}
 Check (-not [Rc2Gui]::IsWindow($dialog)) 'modal closes through keyboard'
 $proc.Refresh(); Check (-not $proc.HasExited) 'closing modal keeps editor alive'
}
$proc=Start-Process -WindowStyle Normal -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session',"`"$session`"") -PassThru
try {
 $script:h=[IntPtr]::Zero
 for($i=0;$i -lt 60;$i++) { Start-Sleep -Milliseconds 100; $proc.Refresh(); if($proc.HasExited){throw 'GUI exited'}; if(([BcTestWindow]::Find($proc.Id)) -ne 0){$script:h=([BcTestWindow]::Find($proc.Id));break} }
 Check ($script:h -ne 0) 'main window'
 $script:view=[Rc2Gui]::GetDlgItem($script:h,2000)
 $panel=[Rc2Gui]::GetDlgItem($script:h,3201)
 $zoomRect=New-Object Rc2Gui+Rect;$modeRect=New-Object Rc2Gui+Rect
 [void][Rc2Gui]::GetWindowRect([Rc2Gui]::GetDlgItem($panel,3104),[ref]$zoomRect)
 [void][Rc2Gui]::GetWindowRect([Rc2Gui]::GetDlgItem($panel,1317),[ref]$modeRect)
 Check ($zoomRect.t -ge $modeRect.b) 'waveform amplitude slider does not cover the treatment buttons'
 $filterRect=New-Object Rc2Gui+Rect;$toolsRect=New-Object Rc2Gui+Rect
 [void][Rc2Gui]::GetWindowRect([Rc2Gui]::GetDlgItem($panel,2511),[ref]$filterRect)
 [void][Rc2Gui]::GetWindowRect([Rc2Gui]::GetDlgItem($panel,2501),[ref]$toolsRect)
 Check ($filterRect.t -ge $toolsRect.b -or $filterRect.b -le $toolsRect.t) 'event filter does not cover Region and Time Selection tools'
 $breaths=[Rc2Gui]::GetDlgItem($h,2557);$clicks=[Rc2Gui]::GetDlgItem($h,2558);$detector=[Rc2Gui]::GetDlgItem($h,2559)
 Check ($breaths -ne [IntPtr]::Zero -and $clicks -ne [IntPtr]::Zero -and $detector -ne [IntPtr]::Zero) 'independent detector controls are visible without opening settings'
 $b=Msg $breaths 0xf0;$c=Msg $clicks 0xf0;$method=Msg $detector 0x147
 $count=Msg $h 0x812d;$history=Msg $h 0x8133
 if((Msg $breaths 0xf0) -eq 0){[void](Msg $breaths 0xf5)}
 if((Msg $clicks 0xf0) -eq 0){[void](Msg $clicks 0xf5)}
 [void](Msg $breaths 0xf5)
 Check ((Msg $breaths 0xf0) -eq 0 -and (Msg $clicks 0xf0) -eq 1) 'click-only mode is available directly in the toolbar'
 [void](Msg $clicks 0xf5)
 Check (-not [Rc2Gui]::IsWindowEnabled([Rc2Gui]::GetDlgItem($h,1200))) 'analysis disabled when neither detector is selected'
 [void](Msg $breaths 0xf5)
 Check ((Msg $breaths 0xf0) -eq 1 -and (Msg $clicks 0xf0) -eq 0) 'breath-only mode is available directly in the toolbar'
 if($b -ne (Msg $breaths 0xf0)){[void](Msg $breaths 0xf5)}
 if($c -ne (Msg $clicks 0xf0)){[void](Msg $clicks 0xf5)}
 Check ((Msg $h 0x812d) -eq $count -and (Msg $h 0x8133) -eq $history) 'changing detectors preserves current proposals and audio Undo history'
 [void](Msg $detector 0x14e 0);[void](Msg $h 0x111 (2559 -bor (1 -shl 16)))
 Check ((Get-Content (Join-Path $env:APPDATA 'BreathClickStudio/analysis_method.txt') -Raw) -eq 'fusion') 'combined detection choice persists in the isolated profile'
 [void](Msg $detector 0x14e $method);[void](Msg $h 0x111 (2559 -bor (1 -shl 16)))
 $tool=[Rc2Gui]::GetDlgItem($panel,2501)
 [void](Msg $tool 0xF5)
 $rect=New-Object Rc2Gui+Rect; [void][Rc2Gui]::GetClientRect($view,[ref]$rect)
 $before=[Rc2Gui]::Pixel($view,(X 40000),($rect.b-4),$null)
 $undo=Msg $h 0x8133 # WM_APP+307
 Drag 19200 67200 $false
 Check ([Rc2Gui]::Captured($view)) 'mouse capture during time drag'
 Check ((Msg $h 0x8137) -gt (Msg $h 0x8136)) 'selection endpoints change during drag'
 $during=[Rc2Gui]::Pixel($view,(X 40000),($rect.b-4),(Join-Path $work 'during-drag.png'))
 Check ($during -ne $before -and $during -eq 0x345c8c) 'painted selection band during drag'
 [void](Msg $view 0x202 0 (MouseParam (X 67200)))
 Check (-not [Rc2Gui]::Captured($view)) 'mouse capture released'
 Check ([Rc2Gui]::Pixel($view,(X 40000),($rect.b-4),$null) -eq $during) 'selection stays visible after release'
 $end=Msg $h 0x8137
 Drag $end 76800
 Check ((Msg $h 0x8137) -gt $end) 'selection boundary can be corrected'
 Check ((Msg $h 0x8133) -eq $undo) 'selection adds no audio Undo'
 $selectionLength=(Msg $h 0x8137)-(Msg $h 0x8136)
 Cmd 1403
 Check ((Msg $h 0x8135) -le ($selectionLength*3+2)) 'Zoom selection fits the selected interval with context'
 Check ((Msg $h 0x8134) -le (Msg $h 0x8136) -and ((Msg $h 0x8134)+(Msg $h 0x8135)) -ge (Msg $h 0x8137)) 'Zoom selection keeps both boundaries visible'
 Check ((Msg $h 0x8133) -eq $undo) 'Zoom selection preserves audio Undo'
 Cmd 1402
 Cmd 2503 # silence, away from all detected events
 Check ((Msg $h 0x812d) -eq 4) 'manual edit in zone with no detector event'
 Check ((Msg $h 0x8133) -eq ($undo+1)) 'one Undo per manual operation'
 Cmd 1208
 $baseFrames=Msg $h 0x8141
 Check ((Msg $h 0x8140) -eq 1) 'preview generated'
 Check ((Msg $h 0x8142 40000) -eq 0) 'manual silence changes preview samples'
 $raw=Msg $h 0x8143 40000
 Check ([math]::Abs($raw) -gt 0) 'Original sample comparison is nonzero'
 # Real transport uses its original/preview buffers. No perceptual assertion.
 Cmd 1510; Cmd 1502
 Check ((Msg $h 0x8144 40000) -eq $raw) 'Original playback receives unprocessed samples'
 Cmd 1511; Cmd 1502
 Check ((Msg $h 0x8144 40000) -eq 0) 'Processed playback receives current preview'
 Cmd 1100
 Check ((Msg $h 0x812d) -eq 3 -and (Msg $h 0x8140) -eq 0) 'Undo removes manual edit and invalidates preview'
 Cmd 1101
 Check ((Msg $h 0x812d) -eq 4) 'Redo restores manual edit'
 Cmd 1106; Drag (Msg $h 0x812e 3) (Msg $h 0x812f 3)
 Check ((Msg $h 0x8137) -gt (Msg $h 0x8136)) 'time interval reselected after Undo/Redo'
 Check ((Msg $view 0x8068 40000) -eq 0) 'silence gain overlay reaches zero'
 $manualUndo=Msg $h 0x8133
 $dialog=OpenModal 2502
 Check ([Rc2Gui]::GetDlgItem($dialog,103) -eq [IntPtr]::Zero) 'manual attenuation has no unused crossfade field'
 SetField $dialog 101 'nan'; SetField $dialog 102 '17'; Key $dialog 13
 Check ([Rc2Gui]::IsWindow($dialog) -and (Msg $h 0x8133) -eq $manualUndo) 'invalid nonfinite attenuation rejected without modifying work'
 SetField $dialog 101 '-6'; Key $dialog 13; Closed $dialog
 Check ((Msg $h 0x812d) -eq 4 -and (Msg $h 0x8133) -eq ($manualUndo+1)) 'changing manual treatment reuses interval with one Undo'
 Cmd 1208
 $factor=[math]::Pow(10,-6/20)
 Check ([math]::Abs((Msg $h 0x8142 40000)-$raw*$factor) -le 2) 'manual Attenuate dialog actually changes rendered samples'
 $gain=Msg $view 0x8068 40000
 Check ([math]::Abs($gain/1000000.0-$factor) -lt .000002) 'gain overlay uses current manual treatment'
 # Actual painted curve must stay visible over the region background.
 $gainY=($rect.b-2)-[int][math]::Round($factor*(($rect.b-2)-26))
 $painted=$false
 foreach($y in (($gainY-1)..($gainY+1))) {
   if([Rc2Gui]::Pixel($view,(X 40000),$y,(Join-Path $work 'attenuation-overlay.png')) -eq 0x96f0aa){$painted=$true}
 }
 Check $painted 'amplitude curve painted over manual region'
 $dialog=OpenModal 2502; SetField $dialog 101 '-30'; Key $dialog 27; Closed $dialog
 Check ((Msg $h 0x8133) -eq ($manualUndo+1) -and (Msg $view 0x8068 40000) -eq $gain) 'Escape cancels attenuation without changing samples or Undo'
 Cmd 1100
 Check ((Msg $view 0x8068 40000) -eq 0) 'Undo restores prior silence treatment'
 $dialog=OpenModal 1204
 Check ([Rc2Gui]::GetDlgItem($dialog,103) -ne [IntPtr]::Zero) 'global settings provide crossfade field'
 SetField $dialog 101 '-9'; SetField $dialog 102 '20'; SetField $dialog 103 '7'; Key $dialog 13; Closed $dialog
 Cmd 1208
 Check ([math]::Abs((Msg $view 0x8068 155000)/1000000.0-[math]::Pow(10,-9/20)) -lt .000002) 'global attenuation applies to existing breath events'
 $gain=Msg $view 0x8068 144480
 $sample=Msg $h 0x8143 144480
 Check ($gain -gt 400000 -and $gain -lt 900000 -and [math]::Abs((Msg $h 0x8142 144480)-$sample*$gain/1000000.) -le 2) 'global fade applies to existing events and matches rendered samples'
 Check ((Msg $view 0x8068 40000) -eq 0) 'global settings preserve explicit manual treatment'
 Cmd 1100
 Check ([math]::Abs((Msg $view 0x8068 155000)/1000000.0-[math]::Pow(10,-12/20)) -lt .000002) 'global event change undone as one operation'
 $dialog=OpenModal 1318 'BreathClickKeepDialog'; SetField $dialog 101 '50'; Key $dialog 13; Closed $dialog
 Cmd 1100 # keep fraction edit on selected manual region
 foreach($mode in @(2504,2505)) {
   Cmd 1106; Drag 90000 120000
   $length=(Msg $h 0x8137)-(Msg $h 0x8136)
   Cmd $mode; Cmd 1208
   $removed=if($mode -eq 2504){$length}else{[math]::Ceiling($length/2)}
   $actualFrames=Msg $h 0x8141
   Check ($actualFrames -eq ($baseFrames-$removed)) "manual timeline duration via GUI command $mode (actual=$actualFrames expected=$($baseFrames-$removed))"
   Cmd 1100
 }
 # Autoscroll continues at the edge even without more mouse movement.
 Cmd 1106; Cmd 1400; Cmd 1400
 $startView=Msg $h 0x8134
 [void](Msg $view 0x201 1 (MouseParam ([int]($rect.r/2))))
 [void](Msg $view 0x200 1 (MouseParam ($rect.r+15)))
 Start-Sleep -Milliseconds 220
 Check ((Msg $h 0x8134) -gt $startView) 'edge autoscroll during held drag'
 [void](Msg $view 0x1f)
 Check (-not [Rc2Gui]::Captured($view)) 'capture loss/cancel ends drag safely'
 Cmd 1402; Cmd 1106
 Cmd 2500; ClickFrame 155000; Cmd 2401 # explicit false positive
 Check ((Msg $h 0x8130 0) -eq 0) 'false positive excluded'
 Cmd 1313 # editorial include must keep negative truth
 Check ((Msg $h 0x8130 0) -eq 1) 'editorial reinclude independent of training'
 Check ([Rc2Gui]::Text([Rc2Gui]::GetDlgItem($panel,3108)) -like 'Incluso:*') 'negative truth label does not mask a reincluded event render state'
 Cmd 1314
 Cmd 2501; Drag 140000 220000; Cmd 2508
 Check ((Msg $h 0x813f) -eq 2) 'events selected by time interval'
 Cmd 1314
 Check ((Msg $h 0x8130 0) -eq 0 -and (Msg $h 0x8130 1) -eq 0) 'bulk exclusion'
 Cmd 1100
 Check ((Msg $h 0x8130 1) -eq 1) 'one Undo restores bulk decision'
 Cmd 2509
 Check ((Msg $h 0x8130 2) -eq 0) 'all click processing disabled'
 Cmd 2510
 Check ((Msg $h 0x8130 2) -eq 1) 'click processing restored'
 Cmd 2500; ClickFrame 240060
 Check ((Msg $h 0x8132) -eq 2) 'click selected independently'
 $clickStart=Msg $h 0x812e 2; $clickEnd=Msg $h 0x812f 2
 $dialog=OpenModal 2311 'BreathClickBoundsDialog'
 [void][Rc2Gui]::PostMessageW($dialog,0x111,[IntPtr]1,[IntPtr]0)
 Start-Sleep -Milliseconds 150
 Check ([Rc2Gui]::IsWindow($h)) 'bounds dialog OK keeps the standalone running'
 Check ((Msg $h 0x812e 2) -eq $clickStart -and (Msg $h 0x812f 2) -eq $clickEnd) "bounds dialog OK preserves exact click samples ($clickStart..$clickEnd -> $(Msg $h 0x812e 2)..$(Msg $h 0x812f 2))"
 Cmd 2540
 $dialog=OpenModal 2554 '#32770'
 Check ([Rc2Gui]::Fields($dialog) -match '6 click' -and [Rc2Gui]::Fields($dialog) -notmatch '6 respiri') 'click profile command reports its own verified examples independently'
 Key $dialog 13;Closed $dialog
 Cmd 2541
 Check ((Msg $h 0x8131 2) -eq 0) 'click false positive excludes click without breath label contamination'
 Cmd 1100
 Check ((Msg $h 0x8131 2) -eq 1) 'Undo restores confirmed click'
 Cmd 2546; Cmd 1208
 Check ((Msg $h 0x8141) -eq $baseFrames) 'dedicated de-click preserves preview duration'
 $originalClick=Msg $h 0x8143 240060
 $processedClick=Msg $h 0x8142 240060
 $feedbackUndo=Msg $h 0x8133
 Cmd 2550; Cmd 1208
 Check ((Msg $h 0x8133) -eq ($feedbackUndo+1) -and (Msg $h 0x8142 240060) -eq $processedClick -and (Msg $h 0x8131 2) -eq 1) 'unsatisfactory repair feedback adds one Undo without changing audio or click truth'
 Cmd 1100; Cmd 1101; Cmd 1208
 Cmd 2548
 Check ([math]::Abs((Msg $h 0x8144 240060)-($originalClick-$processedClick)) -le 2) 'removed-signal player equals original minus processed PCM'
 Cmd 1502
 Cmd 2549; Cmd 1208
 Check ((Msg $h 0x8142 240060) -eq $originalClick) 'None preserves original click without attenuation'
 Cmd 1100; Cmd 1208
 Check ((Msg $h 0x8142 240060) -eq $processedClick) 'Undo restores prior click strategy'
 Cmd 1101; Cmd 1208
 Check ((Msg $h 0x8142 240060) -eq $originalClick) 'Redo restores None repair preference'
 Cmd 2546; Cmd 1208
 Cmd 1315; Cmd 1208
 Check ([math]::Abs((Msg $h 0x8142 240060)-$originalClick*[math]::Pow(10,-12/20.0)) -le 2) 'explicit Attenua on a click applies gain rather than reconstruction'
 Cmd 1100
 Cmd 2500; ClickFrame 155000; ClickFrame 200000 9
 Check ((Msg $h 0x813f) -eq 2) 'Ctrl+click selects multiple events'
 ClickFrame 200000 9
 Check ((Msg $h 0x813f) -eq 1) 'Ctrl+click toggles one event'
 ClickFrame 200000 5
 Check ((Msg $h 0x813f) -eq 2) 'Shift+click selects an event span'
 $filter=[Rc2Gui]::GetDlgItem($panel,2511)
 [void](Msg $filter 0x14e 2); Cmd 2511; Cmd 1322
 Check ((Msg $h 0x813f) -eq 1) 'click filter limits event selection'
 [void](Msg $filter 0x14e 0); Cmd 2511
 Cmd 1207; Cmd 1200 # actual Fusion inference, retaining human negative/manual decisions
 Cmd 1208
 [void][Rc2Gui]::Pixel($h,10,10,(Join-Path $work 'editor.png'))
 # Let the production autosave write the real GUI state, then reopen it.
 Start-Sleep -Seconds 11
 $recovery=Join-Path $env:TEMP 'BreathClick\recovery.bcnproj.json'
 Check (Test-Path $recovery) 'production autosave project exists'
 $project=Get-Content $recovery -Raw | ConvertFrom-Json
 $snapshot=Join-Path (Split-Path $recovery) $project.files[0].session_relative
 $saved=Get-Content $snapshot -Raw | ConvertFrom-Json
 $feedbackRegion=@($saved.regions | Where-Object {$_.kind -eq 1 -and $_.start -eq $clickStart -and $_.end -eq $clickEnd})
 Check ($feedbackRegion.Count -eq 1 -and $feedbackRegion[0].repair_feedback -eq 'unsatisfactory' -and $feedbackRegion[0].repair_feedback_method -eq 'linear') 'repair feedback survives Undo/Redo, method changes, reanalysis and autosave separately from detection labels'
 Check ($saved.regions[0].learning_label -eq 'false_positive' -and $saved.regions[0].training_eligible) 'negative label persists independently of editorial toggles'
 Check (@($saved.regions | Where-Object {$_.manual_operation -and $_.edit_mode -eq 3}).Count -eq 1) 'manual silence persists'
 Check ($saved.effective_detector -eq 'fusion' -and $saved.ai_inference_frames -gt 0) 'Fusion actually executes while preserving human corrections'
 Check ($saved.crossfade_ms -eq 7) 'crossfade settings persist in session'
 $export=Join-Path $work 'export'
 $child=Start-Process -WindowStyle Hidden -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session-export',"`"$snapshot`"","`"$export`"") -PassThru -Wait
 Check ($child.ExitCode -eq 0) 'saved GUI session reopens and exports'
 $outWav=@(Get-ChildItem $export -Filter '*.wav')[0].FullName
 for($frame=0;$frame -lt $baseFrames;$frame+=3013) {
   $expected=(Msg $h 0x8142 $frame)/1000000.0
   $actual=[Rc2Gui]::WavSample($outWav,$frame)
   if([math]::Abs($actual-$expected) -gt .00004){throw "preview/export WAV sample differs at $frame : $expected / $actual"}
 }
 Write-Host 'RC2 OK: GUI preview/export PCM samples within WAV quantization'
 # Reopen the exported MEDIA, not only its session; previous decisions must
 # not be applied a second time to an already processed file.
 $reopened=Start-Process -WindowStyle Normal -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @("`"$outWav`"") -PassThru
 $reopenedHandle=[IntPtr]::Zero
 try {
   for($i=0;$i -lt 60;$i++) {
     Start-Sleep -Milliseconds 100
     $reopenedHandle=[BcTestWindow]::Find($reopened.Id)
     if($reopenedHandle -ne [IntPtr]::Zero){break}
   }
   Check ($reopenedHandle -ne [IntPtr]::Zero) 'exported audio reopens in the real GUI'
   foreach($frame in @(40000,144480,240060)) {
     $actual=(Msg $reopenedHandle 0x8143 $frame)/1000000.0
     Check ([math]::Abs($actual-[Rc2Gui]::WavSample($outWav,$frame)) -le .000002) 'reopened media has exported PCM without a second treatment'
   }
   Check ((Msg $reopenedHandle 0x812d) -eq 0) 'reopened media has no inherited edit regions'
 } finally {
   if(-not $reopened.HasExited){[void][Rc2Gui]::PostMessageW($reopenedHandle,0x10,[IntPtr]0,[IntPtr]0);if(-not $reopened.WaitForExit(2000)){Stop-Process -Id $reopened.Id -Force}}
 }
 # Coextensive detector and manual events must restore to different regions.
 # A detector Cut also makes persisted crossfade observable in output length.
 $overlapSession=Join-Path $work 'overlap.bcnstudio.json'
 $overlapRegions=@(
   @{kind=0;start=144000;end=168000;enabled=$true;approved=$true;edit_mode=1;manual_operation=$false;fade_ms=20;safety_margin_ms=10;decision_source='user';classification='dsp'},
   @{kind=0;start=144000;end=168000;enabled=$true;approved=$true;edit_mode=3;manual_operation=$true;fade_ms=17;decision_source='user';classification='manual'}
 )
 $overlapEvents=@(
   @{kind=0;start=144000;end=168000;enabled=$true;approved=$true;mode='cut';manual_operation=$false;fade_ms=20;safety_margin_ms=10;decision_source='user';classification='dsp'},
   @{kind=0;start=144000;end=168000;enabled=$true;approved=$true;mode='silence';manual_operation=$true;fade_ms=17;decision_source='user';classification='manual'}
 )
 @{schema=2;source=$wav;regions=$overlapRegions;events=$overlapEvents;crossfade_ms=7} | ConvertTo-Json -Depth 6 | Set-Content $overlapSession -Encoding UTF8
 $overlapExport=Join-Path $work 'overlap_export'
 $child=Start-Process -WindowStyle Hidden -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session-export',"`"$overlapSession`"","`"$overlapExport`"") -PassThru -Wait
 Check ($child.ExitCode -eq 0) 'coextensive detector/manual session reopens and exports'
 $overlapWav=@(Get-ChildItem $overlapExport -Filter '*.wav')[0].FullName
 Check ([Rc2Gui]::WavFrames($overlapWav) -eq ($baseFrames-24000+960-336)) 'restore keeps distinct modes and crossfades only inside approved margins'
 # A second, empty document with a deliberately permissive fixture threshold
 # exercises nonzero AI proposals. The first fixture may legitimately yield
 # only clicks after Fusion filtering; no accuracy claim follows from either.
 $freshRoot=Join-Path $work 'fresh_fusion'
 New-Item -ItemType Directory -Path $freshRoot | Out-Null
 $freshSession=Join-Path $freshRoot 'input.bcnstudio.json'
 @{schema=2;source=$wav;regions=@();events=@();corrections=@();ai_threshold=0.0} | ConvertTo-Json -Depth 5 | Set-Content $freshSession -Encoding UTF8
 $savedTemp=$env:TEMP; $savedTmp=$env:TMP
 $env:TEMP=$freshRoot; $env:TMP=$freshRoot
 try { $fusionProc=Start-Process -WindowStyle Normal -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session',"`"$freshSession`"") -PassThru }
 finally { $env:TEMP=$savedTemp; $env:TMP=$savedTmp }
 $primaryHandle=$script:h
 try {
   for($i=0;$i -lt 60;$i++) { Start-Sleep -Milliseconds 100; $fusionProc.Refresh(); if(([BcTestWindow]::Find($fusionProc.Id)) -ne 0){break} }
   $script:h=([BcTestWindow]::Find($fusionProc.Id))
   Check ($script:h -ne 0) 'fresh Fusion fixture opened'
   Cmd 1207; Cmd 1200
   Start-Sleep -Seconds 11
   $fp=Join-Path $freshRoot 'BreathClick\recovery.bcnproj.json'
   $fproj=Get-Content $fp -Raw | ConvertFrom-Json
   $fs=Get-Content (Join-Path (Split-Path $fp) $fproj.files[0].session_relative) -Raw | ConvertFrom-Json
   Check (@($fs.regions | Where-Object {$_.ai_score -gt 0 -and $_.classification -in @('ai','shared')}).Count -gt 0) 'nonzero AI provenance survives GUI analysis and session serialization'
 } finally {
   if(-not $fusionProc.HasExited){[void][Rc2Gui]::PostMessageW($script:h,0x10,[IntPtr]0,[IntPtr]0); if(-not $fusionProc.WaitForExit(2000)){Stop-Process -Id $fusionProc.Id -Force}}
   $script:h=$primaryHandle
 }
 Check ((Hash $wav) -eq $original) 'original hash unchanged'
 Cmd 1402; Cmd 1400; Cmd 1400; Cmd 1400; Cmd 2501; Cmd 1106
 $missingStart=(Msg $h 0x8134)+[int64](((Msg $h 0x8135)-(Msg $h 0x8134))*.25)
 Drag $missingStart ($missingStart+960)
 Check ((Msg $h 0x8137) -gt (Msg $h 0x8136)) 'short missing click time range selected at an editing zoom'
 $beforeMissing=Msg $h 0x812d
 Cmd 2553
 Check ((Msg $h 0x812d) -eq ($beforeMissing+1)) 'one command reports a missing click on the time selection'
 Cmd 1100
 Check ((Msg $h 0x812d) -eq $beforeMissing) 'one Undo removes the reported missing click'
 # Dedicated private-data-free render outcomes: a repairable impulse, an unsafe
 # 553-sample speech reconstruction and a review-only proposal on the same file.
 $outcomeAudio=Join-Path $work 'outcomes.wav';[Rc2Gui]::OutcomeFixture($outcomeAudio)
 $outcomeRegions=@(
  @{kind=1;start=47980;end=48021;repair=$true;repair_method='linear';enabled=$true;approved=$true;edit_mode=0;classification='digital_click'},
  @{kind=1;start=96000;end=96553;repair=$true;repair_method='linear';enabled=$true;approved=$true;edit_mode=0;classification='speech_overlap'},
  @{kind=1;start=144000;end=144120;repair=$true;repair_method='auto';enabled=$true;approved=$false;edit_mode=0;classification='speech_overlap'}
 )
 $outcomeSession=Join-Path $work 'outcomes.bcnstudio.json'
 @{schema=2;source=$outcomeAudio;regions=$outcomeRegions;events=@();crossfade_ms=0} | ConvertTo-Json -Depth 6 | Set-Content $outcomeSession -Encoding UTF8
 $primaryHandle=$script:h;$primaryView=$script:view
 $outcomeProc=Start-Process -WindowStyle Normal -FilePath (Join-Path $bin 'bc_studio.exe') -ArgumentList @('--session',"`"$outcomeSession`"") -PassThru
 try {
  for($i=0;$i -lt 60;$i++){Start-Sleep -Milliseconds 100;$script:h=[BcTestWindow]::Find($outcomeProc.Id);if($script:h -ne 0){break}}
  Check ($script:h -ne 0) 'outcome fixture opens in the real editor'
  $script:view=[Rc2Gui]::GetDlgItem($script:h,2000)
  $outcomePanel=[Rc2Gui]::GetDlgItem($script:h,3201)
  $outcomeState=[Rc2Gui]::GetDlgItem($outcomePanel,3108)
  ClickFrame 96050
  Check ([Rc2Gui]::Text($outcomeState) -like 'Incluso:*') 'inclusion does not claim a successful repair before preview'
  Cmd 1208
  Check ([Rc2Gui]::Text($outcomeState) -eq 'Click intatto: protezione della voce') 'unsafe manual Linear is visibly intact for safety'
  foreach($frame in @(96000,96200,96552)) {Check ((Msg $script:h 0x8142 $frame) -eq (Msg $script:h 0x8143 $frame)) 'safety refusal preserves voice PCM'}
  ClickFrame 48000
  Check ([Rc2Gui]::Text($outcomeState) -eq 'Click riparato') 'successful Linear reconstruction is visibly repaired'
  ClickFrame 144050
  Check ([Rc2Gui]::Text($outcomeState) -eq 'Rilevato: da rivedere') 'uncertain proposal is visibly awaiting review'
  ClickFrame 96050;Cmd 2547;Cmd 1208
  Check ([Rc2Gui]::Text($outcomeState) -eq 'Click intatto: protezione della voce') 'manual Burg retains the same protection'
  Cmd 1100
  Check ([Rc2Gui]::Text($outcomeState) -like 'Incluso:*') 'Undo invalidates the old rendered outcome'
  Cmd 1208
  Check ([Rc2Gui]::Text($outcomeState) -eq 'Click intatto: protezione della voce') 'Undo restores protected Linear outcome on re-preview'
  # Clear regions deliberately does not send the preview invalidation callback.
  # Processed audition must compare the actual plan at the moment of the request.
  Cmd 1202
  Check ((Msg $script:h 0x8140) -eq 1) 'test retains a cached preview after an unnotified document change'
  Cmd 1511;Cmd 1502
  Check ((Msg $script:h 0x8142 48000) -eq (Msg $script:h 0x8143 48000)) 'processed playback never reuses repair coordinates after regions change'
 } finally {
  if(-not $outcomeProc.HasExited){[void][Rc2Gui]::PostMessageW($script:h,0x10,[IntPtr]0,[IntPtr]0);if(-not $outcomeProc.WaitForExit(2000)){Stop-Process -Id $outcomeProc.Id -Force}}
  $script:h=$primaryHandle;$script:view=$primaryView
 }
 Write-Host "RC2 GUI PASSED; DPI=$([Rc2Gui]::GetDpiForWindow($view)); artifacts=$work"
$passed=$true
} finally {
 if(-not $proc.HasExited){[void][Rc2Gui]::PostMessageW($script:h,0x10,[IntPtr]0,[IntPtr]0); if(-not $proc.WaitForExit(2000)){Stop-Process -Id $proc.Id -Force}}
 Close-BcTestWorkspace $workspace $passed
}
