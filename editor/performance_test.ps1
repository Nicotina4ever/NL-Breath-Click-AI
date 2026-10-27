# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  performance_test.ps1 — misura il ridisegno Win32 reale con 600 eventi.
#
#  Il trascinamento e la sua correttezza sono coperti dal test GUI reale
#  (bc_studio_gui_interaction). Qui si misura il costo del ridisegno completo
#  della waveform con 600 regioni: e' la parte che rendeva scattoso il drag.
# ============================================================================
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'test_window.ps1')
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class PerfGui {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$root = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$studio = Join-Path $testBin 'bc_studio.exe'
$gen = Join-Path $testBin 'bc_cli.exe'
$work = Join-Path $env:TEMP ('bc_perf_test_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
$wav = Join-Path $work 'fixture.wav'
& $gen gen-test $wav --quiet 2>&1 | Out-Null
if (-not (Test-Path $wav)) { throw 'WAV di prova non generato' }

$regions = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt 600; $i++) {
    $start = $i * 500
    [void]$regions.Add([ordered]@{
        kind = 0; start = $start; end = $start + 400; reduction_db = -12.0;
        enabled = $true; approved = $true; from_detection = $true;
        edit_mode = 0; keep_fraction = 0.5; fade_ms = 15.0;
        safety_margin_ms = 0.0; decision_source = 'automatic';
        classification = 'dsp'; training_eligible = $false
    })
}
$session = [ordered]@{
    schema = 2; source = $wav; source_relative = '';
    reduction_db = -12.0; fade_ms = 15.0; master_gain_db = 0.0;
    regions = $regions; corrections = @(); events = @()
}
$sessionPath = Join-Path $work 'perf.bcnstudio.json'
$session | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding UTF8

$proc = Start-Process -WindowStyle Normal -FilePath $studio -ArgumentList @('--session', "`"$sessionPath`"") -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 50; $i++) {
    Start-Sleep -Milliseconds 200
    $proc.Refresh()
    if ($proc.HasExited) { throw "studio terminato ($($proc.ExitCode))" }
    if (([BcTestWindow]::Find($proc.Id)) -ne [IntPtr]::Zero) { $hwnd = ([BcTestWindow]::Find($proc.Id)); break }
}
if ($hwnd -eq [IntPtr]::Zero) { throw 'finestra GUI non comparsa' }
Start-Sleep -Milliseconds 900
$view = [PerfGui]::GetDlgItem($hwnd, 2000)
if ($view -eq [IntPtr]::Zero) { throw 'waveform non trovata' }

# Riscaldamento (cache picchi, font, buffer).
for ($i = 0; $i -lt 5; $i++) { [void][PerfGui]::SendMessageW($view, 0x8067, [IntPtr]0, [IntPtr]0) }
$watch = [Diagnostics.Stopwatch]::StartNew()
$frames = 30
for ($i = 0; $i -lt $frames; $i++) {
    [void][PerfGui]::SendMessageW($view, 0x8067, [IntPtr]0, [IntPtr]0)  # WM_APP+103
}
$watch.Stop()
$perFrame = $watch.Elapsed.TotalMilliseconds / $frames
$fps = if ($perFrame -gt 0) { 1000.0 / $perFrame } else { 0 }
Write-Host ("PERFORMANCE: {0:N2} ms/ridisegno, {1:N0} FPS di ridisegno (600 eventi)" -f $perFrame, $fps)

[void][PerfGui]::PostMessageW($hwnd, 0x0010, [IntPtr]0, [IntPtr]0)
Start-Sleep -Milliseconds 500
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Remove-Item -Recurse -Force $work
if ($perFrame -gt 50.0) { throw "ridisegno troppo lento: $perFrame ms" }
Write-Host 'PERFORMANCE TEST OK'
