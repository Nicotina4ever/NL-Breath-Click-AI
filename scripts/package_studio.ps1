# SPDX-License-Identifier: MPL-2.0
# Build a version-checked portable candidate from an explicitly selected build.
[CmdletBinding()]
param(
    [string]$Version = '1.0.0-rc.3',
    [string]$BuildDirectory = '',
    [string]$DependencyRoot = '',
    [string]$ModelDirectory = '',
    [string]$SourceCommit = '',
    [string]$RuntimeDirectory = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $root 'build\bin\Release' }
if (-not $DependencyRoot) { $DependencyRoot = $root }
if (-not $ModelDirectory) { $ModelDirectory = Join-Path $root 'models\generale' }
$build = [IO.Path]::GetFullPath($BuildDirectory)
& (Join-Path $PSScriptRoot 'verify_dependencies.ps1') -DependencyRoot $DependencyRoot -ModelDirectory $ModelDirectory
$exe = Join-Path $build 'bc_studio.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Missing build: $exe" }
$builtVersion = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($builtVersion -ne $Version) { throw "Version mismatch: binary=$builtVersion package=$Version" }
if (-not $SourceCommit) { throw 'Release packaging requires an explicit public SourceCommit' }
$capabilities = & (Join-Path $build 'bc_studio_cli.exe') capabilities | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $capabilities.source_commit -ne $SourceCommit) { throw 'Binary source provenance does not match requested public commit; reconfigure BC_SOURCE_COMMIT and rebuild' }

$stage = Join-Path $root ('build\package_studio_' + $Version + '_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
$files = @{}
foreach ($name in @('bc_studio.exe','bc_studio_cli.exe','bc_cli.exe','libmp3lame.dll','onnxruntime.dll')) {
    $files[$name] = Join-Path $build $name
}
foreach ($name in @('respiro-en-dinamico.onnx','respiro-en-dinamico.onnx.data')) {
    $files["models\generale\$name"] = Join-Path $ModelDirectory $name
}
foreach ($name in @('manifest_respiro-en.json','ATTRIBUZIONE.txt')) {
    $files["models\generale\$name"] = Join-Path $root "models\generale\$name"
}
foreach ($name in @('README.md','LEGGIMI_AVVIO.txt')) {
    $files[$name] = Join-Path $root "packaging\$name"
}
$files['GUIDA_STUDIO.md'] = Join-Path $root 'docs\USER_GUIDE.md'
if ($Version -eq '1.0.0-rc.3') {
    $files['PROVA_RC3.md'] = Join-Path $root 'docs\RELEASE_1.0.0-rc.3.md'
}
$files['TESTING.md'] = Join-Path $root 'docs\TESTING.md'
$files['LICENSE'] = Join-Path $root 'LICENSE'
$files['CHANGELOG.md'] = Join-Path $root "docs\RELEASE_$Version.md"
$files['THIRD_PARTY_NOTICES.md'] = Join-Path $root 'THIRD_PARTY_NOTICES.md'
$files['DEPENDENCIES.lock.json'] = Join-Path $root 'docs\DEPENDENCIES.lock.json'
foreach ($source in Get-ChildItem -LiteralPath (Join-Path $DependencyRoot 'third_party\lame') -File -Recurse) {
    $files['sources\lame\'+$source.FullName.Substring((Join-Path $DependencyRoot 'third_party\lame').Length+1)] = $source.FullName
}
foreach ($license in Get-ChildItem -LiteralPath (Join-Path $root 'LICENSES') -File) {
    $files["LICENSES\$($license.Name)"] = $license.FullName
}
foreach ($entry in $files.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) { throw "Missing package input: $($entry.Value)" }
    $target = Join-Path $stage $entry.Key
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $entry.Value -Destination $target
}
# Runtime is copied from the installed VS redistributable, never from PATH.
if (-not $RuntimeDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installs = @(& $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    foreach ($install in $installs) {
        $redistRoot = Join-Path $install 'VC\Redist\MSVC'
        if (-not (Test-Path -LiteralPath $redistRoot)) { continue }
        $versions = Get-ChildItem -LiteralPath $redistRoot -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+' } | Sort-Object Name -Descending
        foreach ($redist in $versions) {
            $candidate = Join-Path $redist.FullName 'x64\Microsoft.VC143.CRT'
            if (Test-Path -LiteralPath (Join-Path $candidate 'msvcp140.dll')) {
                $RuntimeDirectory = $candidate
                break
            }
        }
        if ($RuntimeDirectory) { break }
    }
    if (-not $RuntimeDirectory) { throw 'VC++ redistributable not found; provide -RuntimeDirectory' }
}
$dlls = @(Get-ChildItem -LiteralPath $RuntimeDirectory -Filter '*.dll')
if ($dlls.Count -eq 0) { throw 'VC++ redistributable not found; provide -RuntimeDirectory' }
$dlls | Copy-Item -Destination $stage
Set-Content -LiteralPath (Join-Path $stage 'VERSION.txt') -Value "NL Breath Click Studio $Version - release candidate" -Encoding UTF8
$revision = if ($SourceCommit) { $SourceCommit } else { (& git -C $root rev-parse HEAD).Trim() }
$buildInfo = [ordered]@{ version = $Version; source_commit = $revision; configuration = 'Release';
    platform = 'Windows x64'; onnx_runtime = (Get-Item -LiteralPath (Join-Path $build 'onnxruntime.dll')).VersionInfo.FileVersion;
    vc_runtime = (Get-Item -LiteralPath (Join-Path $RuntimeDirectory 'msvcp140.dll')).VersionInfo.FileVersion;
    packaging_script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant() }
$buildInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'BUILD_INFO.json') -Encoding UTF8
$manifest = foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName) {
    [ordered]@{ path = $file.FullName.Substring($stage.Length + 1).Replace('\','/'); bytes = $file.Length;
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
ConvertTo-Json -InputObject @($manifest) -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'PACKAGE_MANIFEST.json') -Encoding UTF8
$dist = if ($OutputDirectory) {
    if ([IO.Path]::IsPathRooted($OutputDirectory)) { [IO.Path]::GetFullPath($OutputDirectory) }
    else { [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory)) }
} else { Join-Path $root 'dist' }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zip = Join-Path $dist "NL_Breath_Click_Studio_Windows_Standalone_$Version.zip"
$pending = Join-Path $dist ("NL_Breath_Click_Studio_Windows_Standalone_$Version.pending_" + [guid]::NewGuid().ToString('N') + '.zip')
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $pending -CompressionLevel Optimal
# Replace only after the pending archive is complete; no backup/history copy.
if (Test-Path -LiteralPath $zip) { [IO.File]::Replace($pending,$zip,[NullString]::Value) }
else { Move-Item -LiteralPath $pending -Destination $zip }
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path $zip -Leaf)" | Set-Content -LiteralPath "$zip.sha256" -Encoding ASCII
Write-Host "PACKAGE OK: $zip"
Write-Host "SHA256: $hash"
$stagePath=[IO.Path]::GetFullPath($stage)
$stageRoot=[IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
if(-not $stagePath.StartsWith($stageRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid staging cleanup path'}
Remove-Item -LiteralPath $stagePath -Recurse -Force
Write-Host 'Temporary stage removed after packaging'
