# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
    [string]$DependencyRoot = '',
    [string]$ModelDirectory = '',
    [string]$RuntimeFile = '',
    [switch]$SourcesOnly
)
$ErrorActionPreference = 'Stop'
if (-not $DependencyRoot) { $DependencyRoot = Split-Path -Parent $PSScriptRoot }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\docs\DEPENDENCIES.lock.json') -Raw | ConvertFrom-Json
$base = [IO.Path]::GetFullPath($DependencyRoot).TrimEnd('\') + '\'
$checked=0
foreach ($entry in $manifest.files) {
    if($SourcesOnly -and ($entry.path -match '\.(dll|onnx|onnx.data)$')){continue}
    $path = [IO.Path]::GetFullPath((Join-Path $base $entry.path))
    if (-not $path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Dependency path outside root' }
    if ($ModelDirectory -and $entry.path.StartsWith('models/generale/')) {
        $path = Join-Path $ModelDirectory ([IO.Path]::GetFileName($entry.path))
    }
    if($RuntimeFile -and $entry.path -eq 'third_party/onnxruntime/onnxruntime.dll'){$path=$RuntimeFile}
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing pinned dependency: $path" }
    $checked++
    if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Dependency fingerprint mismatch: $path. No files replaced or downloaded."
    }
}
Write-Host "DEPENDENCIES OK: $($checked) pinned files; no download or replacement"
