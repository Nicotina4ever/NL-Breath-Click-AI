# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  workflow_test.ps1 — import -> analizza tutti -> approva -> esporta tutti.
#
#  Tre WAV sintetici, nessun audio utente, nessun REAPER. Verifica il flusso
#  completo della GUI in modalita' headless e l'integrita' degli originali.
# ============================================================================
param([string]$AudioFixtureDirectory='')
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$studio = Join-Path $testBin 'bc_studio.exe'
$gen = Join-Path $testBin 'bc_cli.exe'
$work = Join-Path $env:TEMP 'bc_workflow_test'
if (Test-Path -LiteralPath $work) { Remove-Item -Recurse -Force $work }
$inputDir = Join-Path $work 'input'
$outputDir = Join-Path $work 'output'
New-Item -ItemType Directory -Force $inputDir,$outputDir | Out-Null

if (-not (Test-Path -LiteralPath $studio)) { throw "bc_studio.exe non trovato: $studio" }
if (-not (Test-Path -LiteralPath $gen)) { throw "bc_cli.exe non trovato: $gen" }

function Hash($path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "") }
    finally { $sha.Dispose() }
}

$inputs = @()
$hashes = @{}
$realFixtures=@()
if ($AudioFixtureDirectory) {
    $fixtureDirectory=(Resolve-Path -LiteralPath $AudioFixtureDirectory).Path
    $buildPrefix=[IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
    if(-not $fixtureDirectory.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Only build-local copies are allowed'}
    $realFixtures=@(Get-ChildItem -LiteralPath $fixtureDirectory -Recurse -File -Filter 'original.wav' | Sort-Object FullName)
    if($realFixtures.Count -ne 3){throw 'This workflow requires exactly three original.wav copies'}
}
for ($i = 1; $i -le 3; $i++) {
    $wav = Join-Path $inputDir ("spot_$i.wav")
    if($realFixtures.Count) { Copy-Item -LiteralPath $realFixtures[$i-1].FullName -Destination $wav }
    else { & $gen gen-test $wav --quiet 2>&1 | Out-Null }
    if (-not (Test-Path -LiteralPath $wav)) { throw "WAV non generato: $wav" }
    $inputs += $wav
    $hashes[$wav] = Hash $wav
}

$arguments = @('--workflow-test', "`"$outputDir`"")
foreach ($wav in $inputs) { $arguments += "`"$wav`"" }
$process = Start-Process -WindowStyle Hidden -FilePath $studio -ArgumentList $arguments -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "bc_studio --workflow-test ha restituito $($process.ExitCode)" }

$outputs = @(Get-ChildItem -LiteralPath $outputDir -Filter '*_breathclick.wav')
if ($outputs.Count -ne 3) { throw "WAV di uscita: $($outputs.Count), attesi 3" }
$reports = @(Get-ChildItem -LiteralPath $outputDir -Filter '*.edit.json')
$plans = @(Get-ChildItem -LiteralPath $outputDir -Filter '*.plan.json')
if ($reports.Count -ne 3) { throw "report edit: $($reports.Count)" }
if ($plans.Count -ne 3) { throw "piani: $($plans.Count)" }
if (-not (Test-Path -LiteralPath (Join-Path $outputDir 'breathclick_export_report.json'))) {
    throw 'report di export assente'
}

foreach ($wav in $inputs) {
    if ((Hash $wav) -ne $hashes[$wav]) { throw "originale modificato: $wav" }
}

foreach ($report in $reports) {
    $data = Get-Content -LiteralPath $report.FullName -Raw | ConvertFrom-Json
    if ($data.edited_frames -le 0) { throw "edited_frames non valido: $($report.Name)" }
    if ($data.time_map.Count -lt 1) { throw "time_map vuota: $($report.Name)" }
    $propertyNames = $data.PSObject.Properties.Name
    if ($propertyNames -notcontains 'voice_markers') { throw "voice_markers assenti: $($report.Name)" }
    if ($propertyNames -notcontains 'project_markers') { throw "project_markers assenti: $($report.Name)" }
}

$manifest = Get-Content -LiteralPath (Join-Path $outputDir 'breathclick_export_report.json') -Raw | ConvertFrom-Json
if ($manifest.files.Count -ne 3) { throw "manifest: $($manifest.files.Count) file" }
foreach ($entry in $manifest.files) {
    if ($entry.status -ne 'elaborato') { throw "stato inatteso: $($entry.status)" }
    if (-not (Test-Path -LiteralPath $entry.output)) { throw "uscita assente: $($entry.output)" }
}

Write-Host 'WORKFLOW TEST OK'
Remove-Item -Recurse -Force $work
exit 0
