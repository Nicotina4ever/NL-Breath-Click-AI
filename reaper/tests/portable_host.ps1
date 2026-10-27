# SPDX-License-Identifier: MPL-2.0
# Source into runners/preview. Never route a test through a guessed REAPER PID.
$script:BcsPortableHostSource=$PSScriptRoot
if(-not ('BcsHostIdentity' -as [type])) {
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public class BcsHostIdentity {
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")]static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static bool Matches(long window,int process){uint p;GetWindowThreadProcessId((IntPtr)window,out p);var b=new StringBuilder(128);GetClassName((IntPtr)window,b,128);return p==process&&b.ToString()=="REAPERwnd";}
 public static bool Dispatch(long window,int process,int action){return action>0&&action<65536&&Matches(window,process)&&PostMessage((IntPtr)window,0x111,(IntPtr)action,IntPtr.Zero);}
 public static void ReplacePointer(string pending,string path){if(System.IO.File.Exists(path))System.IO.File.Replace(pending,path,null);else System.IO.File.Move(pending,path);}
}
'@
}
function New-BcsHostBootstrap([string]$Sandbox) {
    $entry=Join-Path $script:BcsPortableHostSource 'portable_host.lua'
    $entry=$entry.Replace('\','/').Replace("'","\'")
    $bootstrap=Join-Path $Sandbox 'portable_host_bootstrap.lua'
    [IO.File]::WriteAllText($bootstrap,"dofile('$entry')",[Text.UTF8Encoding]::new($false))
    return $bootstrap
}
function Wait-BcsPortableHost([int]$OwnedProcessId,[string]$Report) {
    $repo=Split-Path -Parent (Split-Path -Parent $script:BcsPortableHostSource)
    $python=Join-Path (Split-Path -Parent $repo) 'python_win/python.exe'
    if(-not(Test-Path -LiteralPath $python)){$python=(Get-Command python -ErrorAction Stop).Source}
    & $python (Join-Path $script:BcsPortableHostSource 'host_ready.py') --pid $OwnedProcessId --timeout 90 --report $Report
    if($LASTEXITCODE -ne 0){throw 'Initialization: portable main window/evaluation gate failed; no script dispatched'}
}
function Get-BcsWorkspaceInventory([int]$OwnedProcessId,[string]$Sandbox) {
    $report=Join-Path $Sandbox 'workspace_inventory.json'
    # Removing this one generated report prevents accepting a previous snapshot
    # while the native dispatcher is still executing the new request.
    if(Test-Path -LiteralPath $report) {Remove-Item -LiteralPath $report -Force}
    Send-BcsHostScript -OwnedProcessId $OwnedProcessId -Sandbox $Sandbox -Script (Join-Path $script:BcsPortableHostSource 'workspace_inventory.lua')
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while(-not(Test-Path -LiteralPath $report) -and $clock.Elapsed.TotalSeconds -lt 5) {Start-Sleep -Milliseconds 50}
    if(-not(Test-Path -LiteralPath $report)) {throw 'No fresh portable workspace inventory; no cleanup allowed'}
    return Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
}
function Send-BcsHostScript([int]$OwnedProcessId,[string]$Sandbox,[string]$Script) {
    $record=Join-Path $Sandbox 'host.json'
    $clock=[Diagnostics.Stopwatch]::StartNew();$hostInfo=$null
    while($clock.Elapsed.TotalSeconds -lt 10) {
        if(Test-Path -LiteralPath $record) {
            try {$hostInfo=Get-Content -LiteralPath $record -Raw -Encoding UTF8|ConvertFrom-Json} catch {$hostInfo=$null}
            if($hostInfo){
                $token=[regex]::Match([string]$hostInfo.main_window,'(?:0x)?([0-9a-fA-F]{8,16})')
                $resource=[IO.Path]::GetFullPath([string]$hostInfo.resource)
                if($hostInfo.protocol -eq 2 -and $resource -eq (Join-Path $Sandbox 'portable') -and $token.Success -and
                   [BcsHostIdentity]::Matches([Convert]::ToInt64($token.Groups[1].Value,16),$OwnedProcessId)){break}
                $hostInfo=$null # A previous process may have left a stale record.
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if(-not $hostInfo){throw 'Initialization: no dispatcher bound to this portable PID; no script sent. Restart only the isolated host normally'}
    $resolved=(Resolve-Path -LiteralPath $Script).Path
    $repo=Split-Path -Parent (Split-Path -Parent $script:BcsPortableHostSource)
    $managedBootstrap=$false
    $managedRoot=Join-Path $repo 'build\test_work\managed'
    if($resolved.StartsWith($managedRoot+'\reaper_',[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -eq 'bootstrap.lua' -and (Get-Command Assert-BcWorkspacePath -ErrorAction SilentlyContinue)) {
        $parent=Assert-BcWorkspacePath (Split-Path -Parent $resolved)
        $marker=Join-Path $parent '.nlbc-test-workspace.json'
        $managedBootstrap=(Test-Path -LiteralPath $marker) -and (Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json).owner -eq 'nlbc-synthetic-tests-v1'
    }
    if(-not($managedBootstrap -or $resolved.StartsWith($Sandbox+'\',[StringComparison]::OrdinalIgnoreCase) -or $resolved.StartsWith((Join-Path $repo 'reaper')+'\',[StringComparison]::OrdinalIgnoreCase))) {
        throw 'Initialization: script outside the test/source perimeter'
    }
    $id=[guid]::NewGuid().ToString('N');$request=Join-Path $Sandbox ('host_requests\'+$id+'.request.json')
    $ack=Join-Path $Sandbox ('host_requests\'+$id+'.result.json')
    $json=@{script=$resolved}|ConvertTo-Json -Compress
    [IO.File]::WriteAllText($request+'.tmp',$json,[Text.UTF8Encoding]::new($false));[IO.File]::Move($request+'.tmp',$request)
    $pointer=Join-Path $Sandbox 'host_request.json'
    $pointerText=@{name=([IO.Path]::GetFileName($request))}|ConvertTo-Json -Compress
    [IO.File]::WriteAllText($pointer+'.tmp',$pointerText,[Text.UTF8Encoding]::new($false))
    [BcsHostIdentity]::ReplacePointer($pointer+'.tmp',$pointer)
    if(-not [BcsHostIdentity]::Dispatch([Convert]::ToInt64($token.Groups[1].Value,16),$OwnedProcessId,$hostInfo.action)){throw 'Initialization: native command not delivered to owned host'}
    $clock.Restart()
    while(-not(Test-Path -LiteralPath $ack) -and $clock.Elapsed.TotalSeconds -lt 15){Start-Sleep -Milliseconds 100}
    if(-not(Test-Path -LiteralPath $ack)){throw 'Initialization: portable dispatcher did not acknowledge the request; no fallback process started'}
    $answer=Get-Content -LiteralPath $ack -Raw -Encoding UTF8|ConvertFrom-Json
    if($answer.status -ne 'dispatched' -or $answer.main_window -ne $hostInfo.main_window){throw ('Initialization: '+$answer.error)}
    # The action already owns the decoded request. Do not retain two UUID files
    # forever for each successful dispatch; failure evidence remains available.
    foreach($consumed in @(($request+'.received'),$ack)) {
        if(Test-Path -LiteralPath $consumed) {Remove-Item -LiteralPath $consumed -Force}
    }
}
