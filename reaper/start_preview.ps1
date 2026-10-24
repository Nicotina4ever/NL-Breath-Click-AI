# SPDX-License-Identifier: MPL-2.0
# Interactive entry: reuse one existing portable and one profile; never deploy to REAPER AppData.
[CmdletBinding()]
param([switch]$Visible)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$sandbox=Join-Path $repo 'build\reaper_sandbox'
$portable=Join-Path $sandbox 'portable'
$exe=Join-Path $portable 'reaper.exe'
$pointer=Join-Path $sandbox 'preview.json'
if(-not(Test-Path -LiteralPath $exe) -or -not(Test-Path -LiteralPath $pointer)) { throw 'Ambiente di prova non preparato: consultare docs/REAPER_PROGRESS.md' }
$gate=New-Object Threading.Mutex($false,'Local\BreathClickReaperSandbox')
if(-not $gate.WaitOne(0)) {$gate.Dispose();throw 'Un collaudo usa la portable: attendere la conclusione'}
try {
    foreach($entry in (Get-Content -LiteralPath (Join-Path $portable 'runtime_manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json)) {
        $runtimePath=[IO.Path]::GetFullPath([string]$entry.Path)
        if(-not $runtimePath.StartsWith($portable+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Manifest runtime esterno alla portable'}
        if((Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash -ne $entry.Hash) {throw ('Runtime portable modificato: '+$runtimePath)}
    }
    $info=Get-Content -LiteralPath $pointer -Raw -Encoding UTF8|ConvertFrom-Json
    $project=[IO.Path]::GetFullPath($info.project)
    if(-not $project.StartsWith($sandbox+'\',[StringComparison]::OrdinalIgnoreCase) -or -not(Test-Path -LiteralPath $project)) { throw 'Il progetto di prova non e disponibile' }
    Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public class BcsPreviewWindow {
 public delegate bool E(IntPtr h,IntPtr l);
 [DllImport("user32.dll")]public static extern bool EnumWindows(E e,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 public static void ShowOwned(int process){EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==process){var b=new StringBuilder(256);GetClassName(h,b,256);if(b.ToString()=="REAPERwnd"){ShowWindow(h,9);SetForegroundWindow(h);}}return true;},IntPtr.Zero);}
}
'@
    $running=@(Get-Process reaper -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $exe})
    if($running.Count) {
        if($Visible){[BcsPreviewWindow]::ShowOwned($running[0].Id)}
        Write-Host ('Portable di prova gia aperta, PID '+$running[0].Id+'. Nessuna seconda istanza creata.')
        return
    }
    $env:APPDATA=Join-Path $portable 'appdata'; $env:LOCALAPPDATA=Join-Path $portable 'localappdata'
    $env:CommonProgramFiles=Join-Path $portable 'empty_common'
    $env:CommonProgramW6432=$env:CommonProgramFiles
    ${env:CommonProgramFiles(x86)}=$env:CommonProgramFiles
    New-Item -ItemType Directory -Path (Join-Path $env:CommonProgramFiles 'VST3') -Force|Out-Null
    $env:TEMP=Join-Path $portable 'temp';$env:TMP=$env:TEMP
    New-Item -ItemType Directory -Path $env:TEMP -Force|Out-Null
    $arguments='-newinst -nosplash -cfgfile "'+(Join-Path $portable 'reaper.ini')+'" "'+$project+'" "'+(Join-Path $PSScriptRoot 'preview_bootstrap.lua')+'"'
    $style=if($Visible){'Normal'}else{'Hidden'}
    $child=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle $style
    [ordered]@{pid=$child.Id;project=$project;resource=$portable;started=(Get-Date -Format o)} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $sandbox 'preview_process.json') -Encoding UTF8
    Write-Host ('REAPER preview, PID '+$child.Id+'. Resource: '+$portable)
    Write-Host 'Pannello: NL Breath Click Studio. Progetto e media sono copie sintetiche sacrificabili.'
} finally {$gate.ReleaseMutex();$gate.Dispose()}
