# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  bulk_events_test.ps1 — 600 eventi automatici inclusi, esportati senza
#  approvazioni individuali. Verifica il principio "rilieva, correggi le
#  eccezioni, elabora".
# ============================================================================
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$studio = Join-Path $testBin 'bc_studio.exe'
$gen = Join-Path $testBin 'bc_cli.exe'
$work = Join-Path $env:TEMP 'bc_bulk_events_test'
if (Test-Path -LiteralPath $work) { Remove-Item -Recurse -Force $work }
$inputDir = Join-Path $work 'input'
$outputDir = Join-Path $work 'output'
New-Item -ItemType Directory -Force $inputDir,$outputDir | Out-Null
if (-not (Test-Path -LiteralPath $studio)) { throw "bc_studio.exe non trovato" }
if (-not (Test-Path -LiteralPath $gen)) { throw "bc_cli.exe non trovato" }

$wav = Join-Path $inputDir 'fixture.wav'
& $gen gen-test $wav --quiet 2>&1 | Out-Null
if (-not (Test-Path -LiteralPath $wav)) { throw 'WAV di prova non generato' }
function FileHash($path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}
$hashBefore = FileHash $wav

# 600 regioni non sovrapposte da 400 campioni, ogni 500 campioni. Sono
# proposte automatiche già incluse: nessuna conferma manuale.
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
$sessionPath = Join-Path $work 'bulk.bcnstudio.json'
$session | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding UTF8

$process = Start-Process -WindowStyle Hidden -FilePath $studio `
    -ArgumentList @('--session-export', "`"$sessionPath`"", "`"$outputDir`"") `
    -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "export bulk fallito: exit $($process.ExitCode)" }

$wavOut = @(Get-ChildItem -LiteralPath $outputDir -Filter '*_breathclick.wav')
if ($wavOut.Count -ne 1) { throw "WAV di uscita: $($wavOut.Count)" }
$report = Get-ChildItem -LiteralPath $outputDir -Filter '*.edit.json' | Select-Object -First 1
if (-not $report) { throw 'report edit assente' }
$json = Get-Content -LiteralPath $report.FullName -Raw | ConvertFrom-Json
if ($json.operations.Count -ne 600) { throw "operazioni: $($json.operations.Count), attese 600" }
if ($json.applied_events -ne 600) { throw "applied_events: $($json.applied_events)" }
if ($json.crossfade_frames -ne 0) { throw "crossfade inatteso: $($json.crossfade_frames)" }
if ((FileHash $wav) -ne $hashBefore) { throw 'originale modificato' }

Write-Host "BULK EVENTS TEST OK: $($json.operations.Count) eventi elaborati senza approvazioni"
Remove-Item -Recurse -Force $work
exit 0
