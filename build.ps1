# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  build.ps1 — BreathClick Studio (Windows x64)
#
#  .\build.ps1                 configura e compila Release
#  .\build.ps1 -Test           compila ed esegue CTest
#  .\build.ps1 -Clean          pulisce i target CMake, preservando portable e dati di prova
#  .\build.ps1 -Package        crea/aggiorna il pacchetto in dist
#
#  Non richiede CMake o Visual Studio nel PATH: usa il toolchain locale in
#  tools/cmake-3.28.6-windows-x86_64/ oppure CMake standard.
# ============================================================================
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$Test,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
$cmake = $null
foreach ($candidate in @(
    (Join-Path $root 'tools/cmake-3.28.6-windows-x86_64\bin\cmake.exe'),
    'C:\Program Files\CMake\bin\cmake.exe',
    'C:\Program Files (x86)\CMake\bin\cmake.exe'
)) {
    if (Test-Path -LiteralPath $candidate) { $cmake = $candidate; break }
}
if (-not $cmake) {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { $cmake = $onPath.Source }
}
if (-not $cmake) {
    throw 'CMake non trovato: usare il toolchain locale o installare CMake.'
}

Write-Host "BreathClick Studio — build $Config" -ForegroundColor Cyan
Write-Host "Radice: $root"

if ($Clean -and (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
    # Clean generated targets only. Keep the single REAPER portable and user
    # validation/recovery data in build; do not archive or duplicate the tree.
    & $cmake --build $build --config $Config --target clean
    if ($LASTEXITCODE -ne 0) { throw 'Pulizia target CMake fallita' }
}

$configureOptions = @()
if ($Test -and $Config -eq 'Release') { $configureOptions += '-DBC_REQUIRE_RELEASE_TESTS=ON' }
& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 @configureOptions
if ($LASTEXITCODE -ne 0) { throw 'Configurazione CMake fallita' }
& $cmake --build $build --config $Config
if ($LASTEXITCODE -ne 0) { throw 'Compilazione fallita' }

if ($Test) {
    $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
    & $ctest --test-dir $build -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Alcuni test sono falliti' }
}

if ($Package) {
    $packager = Join-Path $root 'scripts\package_studio.ps1'
    if (-not (Test-Path -LiteralPath $packager)) { throw 'Packager non trovato' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $packager
    if ($LASTEXITCODE -ne 0) { throw 'Packaging fallito' }
}

Write-Host ''
$binPath = Join-Path $build ('bin\' + $Config)
Write-Host ('Build completata: ' + $binPath) -ForegroundColor Green
Write-Host 'Test:     .\build.ps1 -Test'
Write-Host 'Release:  .\build.ps1 -Package'
