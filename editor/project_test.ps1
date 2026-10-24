# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  project_test.ps1 — prova di progetto multi-file senza pilotare dialoghi.
#
#  Crea due WAV sintetici, chiede a bc_studio di salvare un progetto, lo
#  riapre con il percorso diretto e verifica che sessioni e metadati esistano.
#  Non usa audio utente e non avvia REAPER.
# ============================================================================
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$studio = Join-Path $testBin 'bc_studio.exe'
$gen = Join-Path $testBin 'bc_cli.exe'
$work = Join-Path $env:TEMP 'bc_project_gui_test'
if (Test-Path -LiteralPath $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null

if (-not (Test-Path -LiteralPath $studio)) { throw "bc_studio.exe non trovato: $studio" }
if (-not (Test-Path -LiteralPath $gen)) { throw "bc_cli.exe non trovato: $gen" }

$wav1 = Join-Path $work 'spot_a.wav'
$wav2 = Join-Path $work 'spot_b.wav'
& $gen gen-test $wav1 --quiet 2>&1 | Out-Null
& $gen gen-test $wav2 --quiet 2>&1 | Out-Null
if (-not (Test-Path $wav1) -or -not (Test-Path $wav2)) { throw 'WAV di prova non generati' }

function Hash($path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "") }
    finally { $sha.Dispose() }
}
$hash1 = Hash $wav1
$hash2 = Hash $wav2
$project = Join-Path $work 'sessione.bcnproj.json'

$process = Start-Process -WindowStyle Hidden -FilePath $studio `
    -ArgumentList @('--project-test', "`"$wav1`"", "`"$wav2`"", "`"$project`"") `
    -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "bc_studio --project-test ha restituito $($process.ExitCode)" }
if (-not (Test-Path -LiteralPath $project)) { throw 'progetto non scritto' }

$json = Get-Content -LiteralPath $project -Raw | ConvertFrom-Json
if ($json.schema -ne 1) { throw 'schema progetto inatteso' }
if ($json.files.Count -ne 2) { throw "file nel progetto: $($json.files.Count)" }
$sessionDir = Join-Path $work '.breathclick_sessions'
if (-not (Test-Path -LiteralPath $sessionDir)) { throw 'cartella sessioni assente' }
$sessions = @($json.files | ForEach-Object { Get-Item -LiteralPath (Join-Path $work $_.session_relative) })
if ($sessions.Count -ne 2) { throw "sessioni: $($sessions.Count)" }
if ((Hash $wav1) -ne $hash1 -or (Hash $wav2) -ne $hash2) { throw 'un originale e stato modificato' }
foreach ($entry in $json.files) {
    if (-not (Test-Path -LiteralPath $entry.source)) { throw "sorgente assente: $($entry.source)" }
    if (-not $entry.session_relative) { throw 'session_relative vuoto' }
    if ($entry.status -eq 'da_analizzare') { throw 'stato non aggiornato dopo analisi' }
    if (-not $entry.analyzed) { throw 'analyzed falso dopo analisi' }
}

Write-Host 'PROJECT GUI TEST OK'
Remove-Item -Recurse -Force $work
exit 0
