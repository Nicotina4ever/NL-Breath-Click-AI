# SPDX-License-Identifier: MPL-2.0
# User explicitly selects a REAPER resource directory. Does not change audio settings.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ResourceDirectory)
$ErrorActionPreference='Stop'
function File-Sha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}
    finally{$sha.Dispose();$stream.Dispose()}
}

$source=$PSScriptRoot
$resource=[IO.Path]::GetFullPath($ResourceDirectory)
if(-not(Test-Path -LiteralPath (Join-Path $resource 'reaper.ini'))){throw 'Choose the REAPER resource directory containing reaper.ini'}
$parent=Join-Path $resource 'Scripts';New-Item -ItemType Directory -Path $parent -Force|Out-Null
$destination=Join-Path $parent 'NL Breath Click Studio'
# Refuse to discard untracked user files or local modifications during update.
if(Test-Path -LiteralPath $destination){
    $oldManifest=Get-Content -LiteralPath (Join-Path $destination 'PACKAGE_MANIFEST.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $owned=@{'PACKAGE_MANIFEST.json'=$true}
    foreach($entry in $oldManifest){
        $file=[IO.Path]::GetFullPath((Join-Path $destination $entry.path))
        if(-not $file.StartsWith($destination+'\',[StringComparison]::OrdinalIgnoreCase) -or (File-Sha256 $file) -ne $entry.sha256){throw 'Existing installation modified; preserved. Use a separate folder or ReaPack.'}
        $owned[$entry.path.Replace('\','/')]=$true
    }
    foreach($file in Get-ChildItem -LiteralPath $destination -File -Recurse){
        $relative=$file.FullName.Substring($destination.Length+1).Replace('\','/')
        if(-not $owned.ContainsKey($relative)){throw 'Unmanaged files in existing installation; preserved. Move them explicitly before replacing the application.'}
    }
}
$stage=Join-Path $parent ('.nlbc_install_'+[guid]::NewGuid().ToString('N'))
$previous=Join-Path $parent ('.nlbc_previous_'+[guid]::NewGuid().ToString('N'))
$swapped=$false
try {
    New-Item -ItemType Directory -Path $stage|Out-Null
    $manifest=Get-Content -LiteralPath (Join-Path $source 'PACKAGE_MANIFEST.json') -Raw -Encoding UTF8|ConvertFrom-Json
    foreach($entry in $manifest){
        $file=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        if(-not $file.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -or (File-Sha256 $file) -ne $entry.sha256){throw 'Package integrity check failed'}
        $target=Join-Path $stage $entry.path
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force|Out-Null
        Copy-Item -LiteralPath $file -Destination $target
    }
    Copy-Item -LiteralPath (Join-Path $source 'PACKAGE_MANIFEST.json') -Destination $stage
    $engine=Join-Path $stage 'bin/bc_studio_cli.exe'
    $cap=& $engine capabilities|ConvertFrom-Json
    $info=Get-Content -LiteralPath (Join-Path $stage 'BUILD_INFO.json') -Raw -Encoding UTF8|ConvertFrom-Json
    if($LASTEXITCODE -ne 0 -or $cap.protocol_version -ne 1 -or $cap.source_commit -ne $info.source_commit){throw 'Engine protocol/provenance check failed'}
    if(Test-Path -LiteralPath $destination){Move-Item -LiteralPath $destination -Destination $previous}
    Move-Item -LiteralPath $stage -Destination $destination;$swapped=$true
    Write-Host ('Installed. REAPER > Actions > Load ReaScript: '+(Join-Path $destination 'BreathClickStudio.lua'))
}finally{
    if(-not $swapped -and (Test-Path -LiteralPath $previous) -and -not(Test-Path -LiteralPath $destination)){Move-Item -LiteralPath $previous -Destination $destination}
    foreach($path in @($stage,$previous)){
        if(-not(Test-Path -LiteralPath $path)){continue}
        $absolute=[IO.Path]::GetFullPath($path)
        if(-not $absolute.StartsWith($parent+'\',[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $absolute -Leaf) -notmatch '^\.nlbc_(install|previous)_[a-f0-9]+$'){throw 'Invalid installer cleanup target'}
        if($path -eq $previous -and -not $swapped){continue}
        Remove-Item -LiteralPath $absolute -Recurse -Force
    }
}
