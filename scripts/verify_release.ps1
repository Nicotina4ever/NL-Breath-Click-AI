# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  verify_release.ps1 — verifica la distribuzione, non solo il build tree.
#
#  Uso:
#    powershell -File scripts\verify_release.ps1 -ZipPath dist\...zip
#    powershell -File scripts\verify_release.ps1 -ZipPath ... -StaticOnly
#
#  Controlla:
#   - file obbligatori nel pacchetto, incluso libmp3lame.dll;
#   - dipendenze PE degli eseguibili e delle DLL distribuite;
#   - che l'encoder MP3 NON sia un'importazione obbligatoria della GUI;
#   - avvio della GUI estratta con e senza libmp3lame.dll;
#   - selftest della CLI distribuita.
# ============================================================================
param(
    [Parameter(Mandatory = $true)]
    [string]$ZipPath,
    [switch]$StaticOnly,
    [switch]$KeepTemp = $false,
    [string]$PythonExecutable = '',
    [switch]$RunGuiTests
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
. (Join-Path $root 'editor/test_window.ps1')
$zip = if ([System.IO.Path]::IsPathRooted($ZipPath)) {
    [System.IO.Path]::GetFullPath($ZipPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $ZipPath))
}
if (-not (Test-Path -LiteralPath $zip)) { throw "ZIP non trovato: $zip" }

$work = Join-Path $env:TEMP ("bc_release_verify_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$extract = Join-Path $work 'package'
Expand-Archive -LiteralPath $zip -DestinationPath $extract

$required = @(
    'bc_studio.exe',
    'bc_studio_cli.exe',
    'bc_cli.exe',
    'PACKAGE_MANIFEST.json',
    'models\generale\respiro-en-dinamico.onnx.data',
    'libmp3lame.dll',
    'onnxruntime.dll',
    'msvcp140.dll',
    'vcruntime140.dll',
    'vcruntime140_1.dll',
    'THIRD_PARTY_NOTICES.md',
    'LICENSES\LGPL-2.0.txt',
    'LICENSES\LAME-SOURCE-OFFER.txt',
    'sources\lame\CMakeLists.txt',
    'sources\lame\COPYING',
    'sources\lame\libmp3lame\lame.c',
    'LICENSES\dr_libs-Unlicense-MIT-0.txt',
    'models\generale\respiro-en-dinamico.onnx'
)
foreach ($relative in $required) {
    $path = Join-Path $extract $relative
    if (-not (Test-Path -LiteralPath $path)) {
        throw "File obbligatorio assente nel pacchetto: $relative"
    }
}

# Verify the exact package payload before launching it.
$manifest = Get-Content -LiteralPath (Join-Path $extract 'PACKAGE_MANIFEST.json') -Raw | ConvertFrom-Json
$extractPrefix = [IO.Path]::GetFullPath($extract).TrimEnd('\') + '\'
foreach ($entry in $manifest) {
    $payload = [IO.Path]::GetFullPath((Join-Path $extract $entry.path))
    if (-not $payload.StartsWith($extractPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path outside extraction' }
    if (-not (Test-Path -LiteralPath $payload -PathType Leaf) -or
        (Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Package hash mismatch: $($entry.path)" }
}

$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($dumpbin) { $dumpbin = $dumpbin.Source }
if (-not $dumpbin) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $tools = Get-ChildItem (Join-Path $install 'VC\Tools\MSVC') -Directory |
                Sort-Object Name -Descending | Select-Object -First 1
            if ($tools) {
                $candidate = Join-Path $tools.FullName 'bin\Hostx64\x64\dumpbin.exe'
                if (Test-Path -LiteralPath $candidate) { $dumpbin = $candidate }
            }
        }
    }
}
if (-not $dumpbin) { throw 'dumpbin.exe non trovato: impossibile verificare le dipendenze PE' }

$systemDlls = @{}
foreach ($dir in @($env:SystemRoot, (Join-Path $env:SystemRoot 'System32'))) {
    if ($dir -and (Test-Path -LiteralPath $dir)) {
        Get-ChildItem $dir -Filter '*.dll' -ErrorAction SilentlyContinue |
            ForEach-Object { $systemDlls[$_.Name.ToLowerInvariant()] = $true }
    }
}
$systemNames = @(
    'comctl32.dll', 'comdlg32.dll', 'winmm.dll', 'bcrypt.dll', 'shell32.dll',
    'ole32.dll', 'kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll',
    'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll',
    'msvcp140_codecvt_ids.dll', 'vccorlib140.dll', 'concrt140.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll', 'vcruntime140_threads.dll'
)

$peFiles = @(Get-ChildItem $extract -Filter '*.exe') + @(Get-ChildItem $extract -Filter '*.dll')
$allDeps = @{}
foreach ($file in $peFiles) {
    $output = & $dumpbin /dependents $file.FullName 2>&1 | Out-String
    $section = $false
    foreach ($line in ($output -split "`r?`n")) {
        if ($line -match 'Image has the following dependencies') { $section = $true; continue }
        if (-not $section) { continue }
        if ($line -match '^\s+([A-Za-z0-9._-]+\.dll)\s*$') {
            $dep = $Matches[1]
            $key = $dep.ToLowerInvariant()
            if (-not $allDeps.ContainsKey($key)) { $allDeps[$key] = @() }
            $allDeps[$key] += $file.Name
        } elseif ($line -match '^\s*Summary') { break }
    }
}

# L'encoder MP3 deve essere opzionale: nessun EXE deve importarlo all'avvio.
foreach ($exe in @(Get-ChildItem $extract -Filter '*.exe')) {
    $output = & $dumpbin /dependents $exe.FullName 2>&1 | Out-String
    if ($output -match '(?im)^\s+libmp3lame\.dll\s*$') {
        throw "Importazione obbligatoria di libmp3lame.dll in $($exe.Name): il codec non e' opzionale"
    }
}

# Ogni dipendenza non di sistema deve essere nel pacchetto o in Windows.
foreach ($dep in $allDeps.Keys) {
    if ($dep -match '^(msvcp|vcruntime|concrt|vccorlib)') {
        if (-not (Test-Path -LiteralPath (Join-Path $extract $dep))) { throw "VC++ dependency missing from package: $dep" }
        continue
    }
    if ($systemNames -contains $dep -or $systemDlls.ContainsKey($dep) -or $dep -like 'api-ms-win-*') { continue }
    if (-not (Test-Path -LiteralPath (Join-Path $extract $dep))) {
        throw "Dipendenza non risolta nel pacchetto: $dep (richiesta da $($allDeps[$dep] -join ', '))"
    }
}
# Native programs run with only Windows in PATH and a fresh user state.
$savedEnvironment = @{}
foreach ($name in @('PATH','APPDATA','TEMP','TMP','BC_TEST_BIN','BC_STUDIO_CLI','BC_STUDIO_GEN','BC_STUDIO_INPUT','BC_STUDIO_WORK','PYTHONDONTWRITEBYTECODE')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:APPDATA = Join-Path $work 'appdata'
    $env:TEMP = Join-Path $work 'temp'
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force -Path $env:APPDATA,$env:TEMP | Out-Null
    $env:BC_TEST_BIN = $extract
    $env:BC_STUDIO_CLI = Join-Path $extract 'bc_studio_cli.exe'
    $env:BC_STUDIO_GEN = Join-Path $extract 'bc_cli.exe'
    $env:BC_STUDIO_INPUT = $null
    $env:BC_STUDIO_WORK = Join-Path $work 'no_private_fixtures'
    $env:PYTHONDONTWRITEBYTECODE = '1'
# La CLI deve partire dalla cartella estratta, senza PATH di sviluppo.
$cli = Join-Path $extract 'bc_studio_cli.exe'
$cliProcess = Start-Process -WindowStyle Hidden -FilePath $cli -ArgumentList 'selftest' -WorkingDirectory $extract -PassThru -Wait
if ($cliProcess.ExitCode -ne 0) { throw "bc_studio_cli selftest fallito (exit $($cliProcess.ExitCode))" }

if (-not $StaticOnly) {
    # Avvio GUI con il codec presente.
    $gui = Join-Path $extract 'bc_studio.exe'
    $guiProcess = Start-Process -WindowStyle Normal -FilePath $gui -WorkingDirectory $extract -PassThru
    Start-Sleep -Seconds 3
    $guiProcess.Refresh()
    if ($guiProcess.HasExited) { throw "GUI terminata subito (exit $($guiProcess.ExitCode))" }
    [void](Wait-BcEditorWindow -Process $guiProcess)
    Stop-Process -Id $guiProcess.Id -Force

    # Avvio GUI senza encoder: deve restare avviabile; l'errore MP3 e' solo
    # un messaggio al momento dell'esportazione.
    $noDll = Join-Path $work 'package_without_mp3lame'
    Copy-Item -LiteralPath $extract -Destination $noDll -Recurse
    Rename-Item -LiteralPath (Join-Path $noDll 'libmp3lame.dll') -NewName 'libmp3lame.disabled'
    $guiNo = Start-Process -FilePath (Join-Path $noDll 'bc_studio.exe') -WorkingDirectory $noDll -PassThru
    Start-Sleep -Seconds 3
    $guiNo.Refresh()
    if ($guiNo.HasExited) { throw "GUI terminata senza libmp3lame.dll: il codec non e opzionale" }
    [void](Wait-BcEditorWindow -Process $guiNo)
    Stop-Process -Id $guiNo.Id -Force
}

if ($PythonExecutable) {
    $python = [IO.Path]::GetFullPath($PythonExecutable)
    if (-not (Test-Path -LiteralPath $python)) { throw "Validation Python missing: $python" }
    foreach ($test in @('test_studio_pipeline.py','test_edit_engine.py','test_review_end_to_end.py','test_batch_review.py','test_portable_audio.py')) {
        & $python (Join-Path $root "standalone\tests\$test")
        if ($LASTEXITCODE -ne 0) { throw "Extracted package workflow failed: $test" }
    }
    $packageCapabilities = & $cli capabilities | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read packaged engine capabilities' }
    if ($packageCapabilities.mouth_click_engine -ge 1) {
        & $python (Join-Path $root 'tests\test_mouth_click.py') --cli $cli
        if ($LASTEXITCODE -ne 0) { throw 'Extracted package mouth-click audio regression failed' }
    }
    if (-not $StaticOnly) {
        $env:BC_TEST_BIN = $noDll
        $env:BC_STUDIO_CLI = Join-Path $noDll 'bc_studio_cli.exe'
        & $python (Join-Path $root 'standalone\tests\test_portable_audio.py') --codec-absent
        if ($LASTEXITCODE -ne 0) { throw 'Optional-codec workflow failed' }
        $env:BC_TEST_BIN = $extract
        $env:BC_STUDIO_CLI = Join-Path $extract 'bc_studio_cli.exe'
    }
}
if ($RunGuiTests -and -not $StaticOnly) {
    foreach ($test in @('project_test.ps1','workflow_test.ps1','gui_interaction_test.ps1','detection_settings_test.ps1','performance_test.ps1','bulk_events_test.ps1','rc2_workflow_test.ps1')) {
        & (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "editor\$test")
        if ($LASTEXITCODE -ne 0) { throw "Extracted package GUI test failed: $test" }
    }
}
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}

$files = Get-ChildItem $extract -Recurse -File
Write-Host ("RELEASE VERIFY OK: {0} file, {1} dipendenze PE controllate" -f $files.Count, $allDeps.Count)
Write-Host ("ZIP: {0}" -f $zip)
if ($KeepTemp) {
    Write-Host "Validation artifacts retained: $work"
} else {
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $tempPrefix = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if (-not $resolvedWork.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not ([IO.Path]::GetFileName($resolvedWork)).StartsWith('bc_release_verify_')) {
        throw 'Invalid verification cleanup path'
    }
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    Write-Host 'Temporary verification output removed'
}
