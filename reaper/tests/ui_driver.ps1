# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param([Parameter(Mandatory=$true)][int]$OwnedProcessId,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public class BcsNativeInput {
 [StructLayout(LayoutKind.Sequential)] public struct INPUTINFO {public uint cbSize,dwTime;}
 [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref INPUTINFO i);
 public static uint IdleMilliseconds(){var i=new INPUTINFO();i.cbSize=(uint)Marshal.SizeOf(i);if(!GetLastInputInfo(ref i))throw new Exception("Input idle state unavailable");return unchecked((uint)Environment.TickCount-i.dwTime);}
 public delegate bool E(IntPtr h,IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct POINT {public int X,Y;}
 [StructLayout(LayoutKind.Sequential)] public struct RECT {public int L,T,R,B;}
 [DllImport("user32.dll")] public static extern bool EnumWindows(E e,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref POINT p);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int h2,uint flags);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll",SetLastError=true)] public static extern bool GetCursorPos(out POINT p);
 [DllImport("user32.dll",SetLastError=true)] public static extern bool SetCursorPos(int x,int y);
 public static POINT ReadPointer(){POINT p;if(!GetCursorPos(out p))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"GUI input initialization: pointer state unavailable");return p;}
 public static void MovePointer(int x,int y){if(!SetCursorPos(x,y))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"GUI input initialization: pointer move unavailable; no click sent");}
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr e);
 [DllImport("user32.dll")] public static extern void keybd_event(byte k,byte s,uint f,UIntPtr e);
 public static IntPtr Find(int process,string title){IntPtr found=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==process){var b=new StringBuilder(512);GetWindowText(h,b,512);if(b.ToString()==title)found=h;}return true;},IntPtr.Zero);return found;}
 public static void ShowMain(int process){EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==process){var b=new StringBuilder(256);GetClassName(h,b,256);if(b.ToString()=="REAPERwnd")ShowWindow(h,5);}return true;},IntPtr.Zero);}
}
'@
$ready=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'ui_ready.json') -Raw -Encoding UTF8|ConvertFrom-Json
$hostState=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'initialization.json') -Raw -Encoding UTF8|ConvertFrom-Json
if($hostState.status -ne 'ready' -or -not $ready.host_gate -or -not $ready.take_ready) { throw 'REAPER initialization: host, project or BreathClick panel not ready; no GUI input sent' }
$window=[BcsNativeInput]::Find($OwnedProcessId,$ready.title)
if($window -eq [IntPtr]::Zero) { throw 'Owned REAPER panel window not found' }
# Never steal the pointer from an active user. Every subsequent physical
# mouse press also checks that the pointer still matches our owned target.
$idleClock=[Diagnostics.Stopwatch]::StartNew()
while([BcsNativeInput]::IdleMilliseconds() -lt 5000) {
    if($idleClock.Elapsed.TotalSeconds -gt 45) {throw 'Pointer remains in use; native GUI test deferred'}
    Start-Sleep -Milliseconds 100
}
# Recheck after the pointer-idle wait: activation can show a delayed evaluation dialog.
$repoRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$python=Join-Path (Split-Path -Parent $repoRoot) 'python_win/python.exe'
if(-not(Test-Path -LiteralPath $python)){$python=(Get-Command python -ErrorAction Stop).Source}
& $python (Join-Path $PSScriptRoot 'host_ready.py') --pid $OwnedProcessId --timeout 90 --report (Join-Path $EvidenceRoot 'initialization_gui.json')
if($LASTEXITCODE -ne 0){throw 'REAPER initialization: evaluation/main window not ready; no GUI input sent'}
$cursor=[BcsNativeInput]::ReadPointer()
[BcsNativeInput]::ShowMain($OwnedProcessId)
[void][BcsNativeInput]::ShowWindow($window,5)
[void][BcsNativeInput]::SetWindowPos($window,[IntPtr](-1),0,0,0,0,0x43)
[void][BcsNativeInput]::SetForegroundWindow($window)
function Capture([string]$Name) {
    $rect=New-Object BcsNativeInput+RECT; [void][BcsNativeInput]::GetClientRect($window,[ref]$rect)
    $bitmap=New-Object Drawing.Bitmap ($rect.R-$rect.L),($rect.B-$rect.T)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try {
        $dc=$graphics.GetHdc()
        try {if(-not [BcsNativeInput]::PrintWindow($window,$dc,3)){throw 'Owned window capture failed'}}
        finally {$graphics.ReleaseHdc($dc)}
        $bitmap.Save((Join-Path $EvidenceRoot $Name),[Drawing.Imaging.ImageFormat]::Png)
    }
    finally {$graphics.Dispose();$bitmap.Dispose()}
}
function Assert-Pointer([int]$X,[int]$Y) {
    $actual=[BcsNativeInput]::ReadPointer()
    if([Math]::Abs($actual.X-$X) -gt 2 -or [Math]::Abs($actual.Y-$Y) -gt 2){throw "Pointer left target ($X,$Y -> $($actual.X),$($actual.Y)); external input or pointer clipping; no further input sent"}
    $owner=[uint32]0;[void][BcsNativeInput]::GetWindowThreadProcessId([BcsNativeInput]::WindowFromPoint($actual),[ref]$owner)
    if($owner -ne $OwnedProcessId){throw 'Pointer no longer over owned window; GUI test aborted'}
}
function Click-Point([int]$X,[int]$Y) {
    if([BcsNativeInput]::GetForegroundWindow() -ne $window) {
        # REAPER may return focus to arrange after stopping audition. Activate
        # the panel on its empty heading before clicking an actual command.
        $focusPoint=New-Object BcsNativeInput+POINT; $focusPoint.X=10; $focusPoint.Y=10
        [void][BcsNativeInput]::ClientToScreen($window,[ref]$focusPoint)
        [BcsNativeInput]::MovePointer($focusPoint.X,$focusPoint.Y)
        $focusOwner=[uint32]0
        [void][BcsNativeInput]::GetWindowThreadProcessId([BcsNativeInput]::WindowFromPoint($focusPoint),[ref]$focusOwner)
        if($focusOwner -ne $OwnedProcessId){throw 'Owned panel is covered; no input sent'}
        Assert-Pointer $focusPoint.X $focusPoint.Y
        [BcsNativeInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 160
        [BcsNativeInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 160
    }
    $point=New-Object BcsNativeInput+POINT; $point.X=$X; $point.Y=$Y
    [void][BcsNativeInput]::ClientToScreen($window,[ref]$point)
    [void][BcsNativeInput]::SetForegroundWindow($window)
    [BcsNativeInput]::MovePointer($point.X,$point.Y)
    $at=[BcsNativeInput]::WindowFromPoint($point);$owner=[uint32]0
    [void][BcsNativeInput]::GetWindowThreadProcessId($at,[ref]$owner)
    if($owner -ne $OwnedProcessId){throw 'Pointer is not over owned test window; no input sent'}
    Start-Sleep -Milliseconds 90
    Assert-Pointer $point.X $point.Y
    [BcsNativeInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 160
    [BcsNativeInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
}
function Click-Button([string]$Name) {
    $b=$ready.buttons.$Name; if(-not $b){throw "Unknown button $Name"}
    Click-Point ([int]($b.x+$b.w/2)) ([int]($b.y+$b.h/2))
}
function Key([byte]$Code,[bool]$Control=$false) {
    if([BcsNativeInput]::GetForegroundWindow() -ne $window) {throw 'Owned panel lost focus; no keyboard input sent'}
    if($Control){[BcsNativeInput]::keybd_event(17,0,0,[UIntPtr]::Zero)}
    [BcsNativeInput]::keybd_event($Code,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 50
    [BcsNativeInput]::keybd_event($Code,0,2,[UIntPtr]::Zero)
    if($Control){[BcsNativeInput]::keybd_event(17,0,2,[UIntPtr]::Zero)}
}
function Wait-Step([string]$Number) {
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while(-not(Test-Path -LiteralPath (Join-Path $EvidenceRoot "step_$Number.json"))) {
        if((Test-Path -LiteralPath (Join-Path $EvidenceRoot 'result.json')) -or $clock.Elapsed.TotalSeconds -gt 25) {throw "UI step $Number failed; inspect $EvidenceRoot"}
        Start-Sleep -Milliseconds 50
    }
}
function Drag-Boundary {
    Click-Point 10 10
    $point=New-Object BcsNativeInput+POINT
    $point.X=[int]$ready.handles.a.x; $point.Y=[int]$ready.handles.a.y
    [void][BcsNativeInput]::ClientToScreen($window,[ref]$point)
    [BcsNativeInput]::MovePointer($point.X,$point.Y)
    $owner=[uint32]0;[void][BcsNativeInput]::GetWindowThreadProcessId([BcsNativeInput]::WindowFromPoint($point),[ref]$owner)
    if($owner -ne $OwnedProcessId) {throw 'Owned waveform covered; no input sent'}
    Start-Sleep -Milliseconds 90
    Assert-Pointer $point.X $point.Y
    [BcsNativeInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    try {
        Start-Sleep -Milliseconds 150
        for($i=1;$i -le 8;$i++) {Assert-Pointer ($point.X+($i-1)*8) $point.Y;[BcsNativeInput]::MovePointer($point.X+$i*8,$point.Y);Start-Sleep -Milliseconds 35}
        Wait-Step 'drag_live';Capture 'ui_during_drag.png'
    } finally {[BcsNativeInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
    Wait-Step 'drag_saved'
}
try {
    Start-Sleep -Milliseconds 200; Capture 'ui_before.png'
    Drag-Boundary
    Click-Button 'Silenzia'; Wait-Step 'silence'
    Key 90 $true; Wait-Step 'undo_silence'
    Click-Point $ready.row.x $ready.row.y; Wait-Step 'excluded'
    Key 90 $true; Wait-Step 'undo_excluded'
    Click-Button 'Falso positivo';Wait-Step 'false_positive'
    Key 90 $true;Wait-Step 'undo_label'
    Click-Button 'Conferma respiro';Wait-Step 'confirmed'
    Key 90 $true;Wait-Step 'undo_confirmed'
    Key 32; Wait-Step 'play'
    Key 32; Wait-Step 'stop'
    Key 32; Wait-Step 'play_repeat'; Wait-Step 'auto_stop'
    Click-Button 'Dimezza';Wait-Step 'halve'
    Click-Button 'Mantieni sync';Wait-Step 'sync'
    Click-Button 'Originale'; Wait-Step 'original'
    Click-Button 'Elaborato'; Wait-Step 'processed'
    Click-Button 'Analizza'; Wait-Step 'analysis_started'; Wait-Step 'analysis_done'
    Capture 'ui_after.png'
    [IO.File]::WriteAllText((Join-Path $EvidenceRoot 'capture_done'),'done')
    Write-Host 'NATIVE DIRECT GUI OK: live waveform drag, immediate edits, labels, Undo, sync, A/B and reanalysis'
} catch {
    [IO.File]::WriteAllText((Join-Path $EvidenceRoot 'input_aborted'),[string]$_)
    Capture 'ui_failure.png'
    throw
} finally {
    [void][BcsNativeInput]::SetWindowPos($window,[IntPtr](-2),0,0,0,0,3)
    if([BcsNativeInput]::GetForegroundWindow() -eq $window) { [void][BcsNativeInput]::SetCursorPos($cursor.X,$cursor.Y) }
}
