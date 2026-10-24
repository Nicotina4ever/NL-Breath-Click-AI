# SPDX-License-Identifier: MPL-2.0
# Verified first-use backend installation. No professional REAPER configuration.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageDirectory,[string]$ArchivePath='',[string]$Attempt='manual')
$ErrorActionPreference='Stop'
function File-Sha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}
    finally{$sha.Dispose();$stream.Dispose()}
}

$package=[IO.Path]::GetFullPath($PackageDirectory)
$runtime=Join-Path $package 'runtime'
New-Item -ItemType Directory -Path $runtime -Force|Out-Null
$statusPath=Join-Path $runtime 'install-status.json'
function Status($value) {
    $value.attempt=$Attempt
    $pending=$statusPath+'.pending'
    [IO.File]::WriteAllText($pending,($value|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    if(Test-Path -LiteralPath $statusPath){[IO.File]::Replace($pending,$statusPath,[NullString]::Value)}else{[IO.File]::Move($pending,$statusPath)}
}
$stage=$null;$gate=$null;$locked=$false
try {
    $m=Get-Content -LiteralPath (Join-Path $package 'runtime-release.json') -Raw -Encoding UTF8|ConvertFrom-Json
    if($m.schema -ne 1 -or $m.version -notmatch '^[A-Za-z0-9.-]+$' -or $m.sha256 -notmatch '^[a-f0-9]{64}$' -or $m.source_commit -notmatch '^[a-f0-9]{40}$') {throw 'Invalid release manifest'}
    $uri=[Uri]$m.url
    if($uri.Scheme -ne 'https' -or $uri.Host -ne 'github.com' -or -not $uri.AbsolutePath.StartsWith('/Nicotina4ever/NL-Breath-Click-AI/releases/download/')){throw 'Untrusted backend URL'}
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$key=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($package.ToLowerInvariant())))).Replace('-','')}finally{$sha.Dispose()}
    $gate=New-Object Threading.Mutex($false,('Local\NLBC_Runtime_'+$key))
    $locked=$gate.WaitOne(0)
    if(-not $locked){throw 'Another backend installation is running'}
    Status @{status='installing';version=$m.version}
    $destination=Join-Path $runtime $m.version
    if(Test-Path -LiteralPath $destination){
        $ready=Get-Content -LiteralPath (Join-Path $destination 'READY.json') -Raw -Encoding UTF8|ConvertFrom-Json
        if($ready.archive_sha256 -ne $m.sha256 -or $ready.source_commit -ne $m.source_commit){throw 'Installed version differs from this release; preserved for recovery'}
        foreach($entry in $ready.files){
            $file=[IO.Path]::GetFullPath((Join-Path $destination $entry.path))
            if(-not $file.StartsWith($destination+'\',[StringComparison]::OrdinalIgnoreCase) -or (File-Sha256 $file) -ne $entry.sha256){throw 'Installed backend integrity mismatch; no files overwritten'}
        }
        Status @{status='ready';version=$m.version};exit 0
    }
    $stage=Join-Path $runtime ('.install_'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage|Out-Null
    $archive=Join-Path $stage 'backend.zip'
    if($ArchivePath){$archive=[IO.Path]::GetFullPath($ArchivePath)}
    else{[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;Invoke-WebRequest -UseBasicParsing -Uri $m.url -OutFile $archive -TimeoutSec 300}
    if((File-Sha256 $archive) -ne $m.sha256){throw 'Downloaded backend SHA-256 mismatch; nothing installed'}
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    $extract=Join-Path $stage 'extracted';New-Item -ItemType Directory -Path $extract|Out-Null
    try {
        $total=[long]0
        if($zip.Entries.Count -gt 512){throw 'Backend archive has too many entries'}
        foreach($entry in $zip.Entries){
            $target=[IO.Path]::GetFullPath((Join-Path $extract $entry.FullName))
            $total+=$entry.Length
            if($total -gt 268435456 -or -not $target.StartsWith($extract+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe backend archive'}
            if($entry.FullName.EndsWith('/')){continue}
            New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force|Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
        }
    }finally{$zip.Dispose()}
    $info=Get-Content -LiteralPath (Join-Path $extract 'BUILD_INFO.json') -Raw -Encoding UTF8|ConvertFrom-Json
    if($info.source_commit -ne $m.source_commit -or $info.version -ne $m.version){throw 'Backend source/version mismatch'}
    $manifest=Get-Content -LiteralPath (Join-Path $extract 'PACKAGE_MANIFEST.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $install=Join-Path $stage 'install';New-Item -ItemType Directory -Path $install|Out-Null
    $installed=@()
    foreach($entry in $manifest){
        $source=[IO.Path]::GetFullPath((Join-Path $extract $entry.path))
        if(-not $source.StartsWith($extract+'\',[StringComparison]::OrdinalIgnoreCase) -or (File-Sha256 $source) -ne $entry.sha256){throw 'Backend manifest integrity failure'}
        if($entry.path -notmatch '^(bin/|LICENSE$|LICENSES/|sources/|THIRD_PARTY_NOTICES.md$|BUILD_INFO.json$|DEPENDENCIES.lock.json$)'){continue}
        $target=Join-Path $install $entry.path
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force|Out-Null
        Move-Item -LiteralPath $source -Destination $target
        $installed+=@{path=$entry.path;sha256=$entry.sha256}
    }
    $engine=Join-Path $install 'bin/bc_studio_cli.exe'
    $capabilities=& $engine capabilities|ConvertFrom-Json
    if($LASTEXITCODE -ne 0 -or $capabilities.protocol_version -ne 1 -or $capabilities.direct_editing -ne 2 -or $capabilities.source_commit -ne $m.source_commit){throw 'Installed engine protocol/provenance check failed'}
    [IO.File]::WriteAllText((Join-Path $install 'READY.json'),(@{schema=1;version=$m.version;source_commit=$m.source_commit;archive_sha256=$m.sha256;files=$installed}|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $install -Destination $destination
    Status @{status='ready';version=$m.version}
}catch{
    Status @{status='failed';error=$_.Exception.Message};Write-Error $_;exit 1
}finally{
    if($stage -and (Test-Path -LiteralPath $stage)){
        $resolved=[IO.Path]::GetFullPath($stage)
        if(-not $resolved.StartsWith($runtime+'\',[StringComparison]::OrdinalIgnoreCase) -or -not (Split-Path $resolved -Leaf).StartsWith('.install_')){throw 'Invalid installer staging cleanup'}
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    if($locked){$gate.ReleaseMutex()};if($gate){$gate.Dispose()}
}
