# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param([string]$ReaperExecutable='',
      [string]$WorkRoot='', [string]$Script='', [switch]$KeepOpen, [int]$TimeoutSeconds=240,
      [string]$TestProject='', [switch]$RunGuiInputTests,[string]$PackageDirectory='',[switch]$CloseReaPackReports)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(-not $WorkRoot) { $WorkRoot=Join-Path $repo ('build\reaper_sandbox\runs\run_'+[guid]::NewGuid().ToString('N')) }
$WorkRoot=[IO.Path]::GetFullPath($WorkRoot)
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repo 'build'))+'\'
if(-not $WorkRoot.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'REAPER sandbox must be under this repository build directory' }
if((Test-Path -LiteralPath $WorkRoot) -and @(Get-ChildItem -LiteralPath $WorkRoot -Force).Count) { throw 'Use a new evidence directory; existing test data will not be overwritten' }
if(-not $Script) { $Script=Join-Path $PSScriptRoot 'probe.lua' }
$Script=(Resolve-Path -LiteralPath $Script).Path
$portable=Join-Path $repo 'build\reaper_sandbox\portable'
$gate=New-Object Threading.Mutex($false,'Local\BreathClickReaperSandbox')
if(-not $gate.WaitOne(0)) { $gate.Dispose(); throw 'Un altro test usa gia la portable REAPER condivisa' }
try {
$running=@(Get-Process reaper -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $portable 'reaper.exe') })
$reusing=$running.Count -eq 1
if($running.Count -gt 1) { throw 'Initialization: more than one test portable instance' }
if($reusing -and @(Get-Process reaper -ErrorAction SilentlyContinue).Count -ne 1) { throw 'Initialization: another REAPER instance exists; refusing ambiguous -nonewinst routing' }
if($reusing) { $KeepOpen=[switch]$true }
New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $portable 'Plugins') -Force | Out-Null
if(-not $ReaperExecutable) {
    if(Test-Path -LiteralPath (Join-Path $portable 'reaper.exe')) { $ReaperExecutable=Join-Path $portable 'reaper.exe' }
    else { $ReaperExecutable=Join-Path $env:ProgramFiles 'REAPER (x64)\reaper.exe' }
}
$installed=Split-Path -Parent $ReaperExecutable
$runtimeExe=Join-Path $portable 'reaper.exe'
if(-not(Test-Path -LiteralPath $runtimeExe)) { Copy-Item -LiteralPath $ReaperExecutable -Destination $runtimeExe }
foreach($name in @('reaper_wave.dll','reaper_mp3dec.dll','reaper_flac.dll','soundtouch.dll','elastique3.dll','jsfx.dll')) {
    $target=Join-Path $portable ('Plugins\'+$name)
    if(-not(Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath (Join-Path $installed ('Plugins\'+$name)) -Destination $target }
}
if(-not(Test-Path -LiteralPath (Join-Path $portable 'ColorThemes'))) { Copy-Item -LiteralPath (Join-Path $installed 'InstallData\ColorThemes') -Destination $portable -Recurse }
$emptyPlugins=Join-Path $portable 'empty_plugins'
New-Item -ItemType Directory -Path $emptyPlugins -Force | Out-Null
$ini=@'
[REAPER]
showlastproject=0
loadlastproject=0
newprojdo=0
newprojtmpl=
vstpath64=__EMPTY_PLUGIN_DIRECTORY__
vstpath=__EMPTY_PLUGIN_DIRECTORY__
vst_scan=2
au_scan=0
video_searchpaths=
audiomode=0
waveout_driver_out=-1
waveout_driver_in=-1
'@
$ini=$ini.Replace('__EMPTY_PLUGIN_DIRECTORY__',$emptyPlugins)
if(-not(Test-Path -LiteralPath (Join-Path $portable 'reaper.ini'))) {
    [IO.File]::WriteAllText((Join-Path $portable 'reaper.ini'),$ini,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $portable 'reaper-vstplugins64.ini'),"[vstcache]`r`n",[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $portable 'reaper-vstshells64.ini'),"[vstshells]`r`n",[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $portable 'reaper-install-rev.txt'),((Get-Item -LiteralPath $runtimeExe).VersionInfo.ProductVersion+[char]0),[Text.UTF8Encoding]::new($false))
}
$runtimeManifest=Join-Path $portable 'runtime_manifest.json'
if(-not(Test-Path -LiteralPath $runtimeManifest) -or (Get-Item -LiteralPath $runtimeManifest).Length -eq 0) {
    $runtimeFiles=@($runtimeExe)+@(Get-ChildItem -LiteralPath (Join-Path $portable 'Plugins') -File|ForEach-Object {$_.FullName})
    $fingerprints=@(foreach($file in $runtimeFiles) {
        $hash=Get-FileHash -LiteralPath $file -Algorithm SHA256
        $original=Join-Path $installed $file.Substring($portable.Length+1)
        if($hash.Hash -ne (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash) { throw ('Copia runtime diversa dall originale: '+$file) }
        $hash|Select-Object Path,Hash
    })
    $fingerprints | ConvertTo-Json |
        Set-Content -LiteralPath $runtimeManifest -Encoding UTF8
}
foreach($entry in (Get-Content -LiteralPath $runtimeManifest -Raw|ConvertFrom-Json)) {
    if((Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash -ne $entry.Hash) { throw ('Runtime portable modificato: '+$entry.Path) }
}
# JSFX is needed for real take/track FX regression. Add this one pinned runtime
# to the same portable once; never provision another REAPER copy per run.
$fingerprints=@(foreach($entry in (Get-Content -LiteralPath $runtimeManifest -Raw|ConvertFrom-Json)) {
    [pscustomobject]@{Path=[string]$entry.Path;Hash=[string]$entry.Hash}
})
$jsfx=Join-Path $portable 'Plugins\jsfx.dll'
if(-not($fingerprints|Where-Object {$_.Path -eq $jsfx})) {
    $hash=Get-FileHash -LiteralPath $jsfx -Algorithm SHA256
    if($hash.Hash -ne (Get-FileHash -LiteralPath (Join-Path $installed 'Plugins\jsfx.dll') -Algorithm SHA256).Hash) {throw 'JSFX runtime fingerprint mismatch'}
    $fingerprints+=[pscustomobject]@{Path=$hash.Path;Hash=$hash.Hash}
    ConvertTo-Json -InputObject $fingerprints -Depth 4|Set-Content -LiteralPath $runtimeManifest -Encoding UTF8
}
$effects=Join-Path $portable 'Effects'
New-Item -ItemType Directory -Path $effects -Force|Out-Null
$effect=Join-Path $effects 'BreathClickTestGain'
if(-not(Test-Path -LiteralPath $effect)) {
    [IO.File]::WriteAllText($effect,@'
desc:BreathClick isolated test gain
slider1:-6<-60,6,0.1>Gain (dB)
@slider
gain=10^(slider1/20);
@sample
spl0*=gain; spl1*=gain;
'@,[Text.UTF8Encoding]::new($false))
}
$bootstrap=Join-Path $WorkRoot 'bootstrap.lua'
function ConvertTo-LuaLiteral([string]$Value) {
    $equals=''
    while($Value.Contains(']'+$equals+']')){$equals+='='}
    return '['+$equals+'['+$Value+']'+$equals+']'
}
$code="BCS_TEST_ROOT = $(ConvertTo-LuaLiteral $WorkRoot)`nBCS_KEEP_OPEN = $($KeepOpen.IsPresent.ToString().ToLowerInvariant())`n"
$code+="BCS_TEST_IS_GUI = $($RunGuiInputTests.IsPresent.ToString().ToLowerInvariant())`n"
$code+="local expected = $(ConvertTo-LuaLiteral $portable.Replace('\','/'))`n"
$code+="BCS_TEST_RESOURCE = expected`n"
if($PackageDirectory) {
    $PackageDirectory=(Resolve-Path -LiteralPath $PackageDirectory).Path
    if(-not $PackageDirectory.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Only isolated build-local package tests are allowed'}
    $code+="BCS_TEST_PACKAGE_DIR = $(ConvertTo-LuaLiteral $PackageDirectory.Replace('\','/'))`n"
}
if($TestProject) {
    $TestProject=(Resolve-Path -LiteralPath $TestProject).Path
    if(-not $TestProject.StartsWith((Join-Path $repo 'build\reaper_sandbox')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Only a sandbox project may be used for GUI testing' }
    $code+="BCS_TEST_PROJECT = $(ConvertTo-LuaLiteral $TestProject)`n"
}
$code+="assert(reaper.GetResourcePath():gsub('\\','/'):lower() == expected:lower(), 'Sandbox resource mismatch: refusing to run')`n"
if(-not $PackageDirectory) {
    $testEngine=Join-Path $repo 'build/bin/Release/bc_studio_cli.exe'
    $code+="reaper.SetExtState('BreathClickStudio','engine',$(ConvertTo-LuaLiteral $testEngine),false)"+[char]10
}
$codec=ConvertTo-LuaLiteral (Join-Path $PSScriptRoot '..\bc_json.lua')
$code+="local J=dofile($codec); assert(J.write(BCS_TEST_ROOT..'/startup_probe.json',{status='event_loop_available',resource=reaper.GetResourcePath()}))`n"
$code+="local started=reaper.time_precise(); local function launch_when_ready()`n"
$code+="if not reaper.file_exists(BCS_TEST_ROOT..'/host_ready') then if reaper.time_precise()-started<100 then reaper.defer(launch_when_ready) else J.write(BCS_TEST_ROOT..'/result.json',{status='initialization_failed',error='Host readiness gate did not open'}) end; return end`n"
if($reusing) { $code+="reaper.Main_OnCommand(40859,0)`n" }
$code+="local ok,err=xpcall(function() dofile($(ConvertTo-LuaLiteral $Script)) end,debug.traceback)`n"
$code+="if not ok then local J=dofile($codec); local status=(BCS_TEST_IS_GUI and not reaper.file_exists(BCS_TEST_ROOT..'/ui_ready.json')) and 'initialization_failed' or 'failed'; assert(J.write(BCS_TEST_ROOT..'/result.json',{status=status,error=tostring(err)})); if not BCS_KEEP_OPEN then reaper.defer(function()reaper.Main_OnCommand(40004,0)end) end end`n"
$code+="end; reaper.defer(launch_when_ready)`n"
[IO.File]::WriteAllText($bootstrap,$code,[Text.UTF8Encoding]::new($false))
$env:APPDATA=Join-Path $portable 'appdata'; $env:LOCALAPPDATA=Join-Path $portable 'localappdata'
$env:CommonProgramFiles=Join-Path $portable 'empty_common'
$env:CommonProgramW6432=$env:CommonProgramFiles
${env:CommonProgramFiles(x86)}=$env:CommonProgramFiles
$env:TEMP=Join-Path $WorkRoot 'temp'; $env:TMP=$env:TEMP
New-Item -ItemType Directory -Path $env:APPDATA,$env:LOCALAPPDATA,$env:TEMP,(Join-Path $env:CommonProgramFiles 'VST3') -Force | Out-Null
$arguments='-newinst -new -nosplash -noactivate -cfgfile "'+(Join-Path $portable 'reaper.ini')+'" "'+$bootstrap+'"'
if($reusing) {
    $arguments='-nonewinst -noactivate -cfgfile "'+(Join-Path $portable 'reaper.ini')+'" "'+$bootstrap+'"'
    $dispatch=Start-Process -FilePath (Join-Path $portable 'reaper.exe') -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $child=$running[0]
} else {
    $child=Start-Process -FilePath (Join-Path $portable 'reaper.exe') -ArgumentList $arguments -PassThru -WindowStyle Hidden
}
$python=Join-Path (Split-Path -Parent $repo) 'python_win/python.exe'
if(-not(Test-Path -LiteralPath $python)) { $python=(Get-Command python -ErrorAction Stop).Source }
& $python (Join-Path $PSScriptRoot 'host_ready.py') --pid $child.Id --probe (Join-Path $WorkRoot 'startup_probe.json') --timeout 90 --report (Join-Path $WorkRoot 'initialization.json')
if($LASTEXITCODE -ne 0) { throw 'REAPER initialization failed; no BreathClick engine or GUI test was started. See initialization.json' }
[IO.File]::WriteAllText((Join-Path $WorkRoot 'host_ready'),'ready')
[ordered]@{pid=$child.Id;executable=(Join-Path $portable 'reaper.exe');resource=$portable;script=$Script;started=(Get-Date -Format o)} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $WorkRoot 'process.json') -Encoding UTF8
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public class BcsSandboxWindows {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc c,IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p,EnumProc c,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$report=Join-Path $WorkRoot 'result.json'
$clock=[Diagnostics.Stopwatch]::StartNew()
$guiDriven=$false
while(-not(Test-Path -LiteralPath $report) -and -not $child.HasExited -and $clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
    if($RunGuiInputTests -and -not $guiDriven -and (Test-Path -LiteralPath (Join-Path $WorkRoot 'ui_ready.json'))) {
        try { & (Join-Path $PSScriptRoot 'ui_driver.ps1') -OwnedProcessId $child.Id -EvidenceRoot $WorkRoot }
        catch {
            [IO.File]::WriteAllText((Join-Path $WorkRoot 'input_aborted'),[string]$_)
            Write-Warning ([string]$_)
        }
        $guiDriven=$true
    }
    # Optional ReaPack report only; evaluation is handled by the host gate.
    [BcsSandboxWindows]::EnumWindows({param($h,$l)
        $ownerId=[uint32]0; [void][BcsSandboxWindows]::GetWindowThreadProcessId($h,[ref]$ownerId)
        if($ownerId -eq $child.Id) {
            $windowTitle=New-Object Text.StringBuilder 256
            [void][BcsSandboxWindows]::GetWindowText($h,$windowTitle,256)
            $script:ownedReaPackReport=$CloseReaPackReports -and $windowTitle.ToString() -eq 'Transaction report'
            [BcsSandboxWindows]::EnumChildWindows($h,{param($button,$unused)
                $caption=New-Object Text.StringBuilder 256
                [void][BcsSandboxWindows]::GetWindowText($button,$caption,256)
                if($script:ownedReaPackReport -and $caption.ToString() -match '^&?OK$' -and [BcsSandboxWindows]::IsWindowEnabled($button)) {
                    [void][BcsSandboxWindows]::SendMessage($button,0xF5,[IntPtr]::Zero,[IntPtr]::Zero)
                }; return $true
            },[IntPtr]::Zero) | Out-Null
        }; return $true
    },[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 100
}
if($RunGuiInputTests -and -not(Test-Path -LiteralPath (Join-Path $WorkRoot 'ui_ready.json'))) { throw "REAPER GUI initialization failed: project/panel not ready; no GUI input sent. PID $($child.Id), evidence $WorkRoot" }
if(Test-Path -LiteralPath (Join-Path $WorkRoot 'initialization_gui.json')) {
    $guiInitialization=Get-Content -LiteralPath (Join-Path $WorkRoot 'initialization_gui.json') -Raw -Encoding UTF8|ConvertFrom-Json
    if($guiInitialization.status -ne 'ready') { throw 'REAPER GUI initialization failed at evaluation/main window; not a BreathClick engine failure' }
}
if(-not(Test-Path -LiteralPath $report)) { throw "No report after host readiness; owned PID $($child.Id), sandbox $WorkRoot retained for inspection" }
$result=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
$result|ConvertTo-Json -Depth 5
Write-Host "SANDBOX: $WorkRoot; PID $($child.Id)"
if($result.status -ne 'passed') { if(-not $KeepOpen){[void]$child.WaitForExit(10000)};throw 'REAPER integration test failed (see sandbox report)' }
if(-not $KeepOpen) { if(-not $child.WaitForExit(10000)) { throw "Owned test instance did not exit; PID $($child.Id) retained" } }
} finally { $gate.ReleaseMutex(); $gate.Dispose() }
