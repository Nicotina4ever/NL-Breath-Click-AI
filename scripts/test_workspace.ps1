# SPDX-License-Identifier: MPL-2.0
# Owned, bounded scratch slots. Never use these for listening votes or user data.
if($null -eq $script:BcTestWorkspaceActive) {$script:BcTestWorkspaceActive=@{}}
function Assert-BcWorkspacePath([string]$Path) {
    $repo=Split-Path -Parent $PSScriptRoot
    $base=[IO.Path]::GetFullPath((Join-Path $repo 'build/test_work/managed'))
    $full=[IO.Path]::GetFullPath($Path)
    if(-not $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Workspace outside managed test area'}
    $cursor=$full
    while($cursor -and $cursor -ne $repo) {
        if(Test-Path -LiteralPath $cursor) {
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Linked test workspace refused'}
        }
        $cursor=Split-Path -Parent $cursor
    }
    return $full
}
function Clear-BcTestWorkspace([string]$Path) {
    $full=Assert-BcWorkspacePath $Path
    $owner=Join-Path $full '.nlbc-test-workspace.json'
    if(-not(Test-Path -LiteralPath $owner) -or (Get-Content -LiteralPath $owner -Raw|ConvertFrom-Json).owner -ne 'nlbc-synthetic-tests-v1') {throw 'Unowned test directory refused'}
    $entries=@(Get-ChildItem -LiteralPath $full -Force -Recurse)
    if(@($entries|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count) {throw 'Linked test contents refused'}
    foreach($entry in Get-ChildItem -LiteralPath $full -Force) {
        if($entry.Name -ne '.nlbc-test-workspace.json') {Remove-Item -LiteralPath $entry.FullName -Recurse -Force}
    }
}
function New-BcTestWorkspace([string]$Name) {
    if($Name -notmatch '^[a-z0-9_]+$') {throw 'Invalid test workspace name'}
    $repo=Split-Path -Parent $PSScriptRoot
    $path=Assert-BcWorkspacePath (Join-Path $repo ('build/test_work/managed/'+$Name))
    if($script:BcTestWorkspaceActive.ContainsKey($path)) {throw "Test workspace busy: $Name"}
    $gate=New-Object Threading.Mutex($false,('Local\NLBCSTestWorkspace_'+$Name))
    $locked=$false
    try {
        try {$locked=$gate.WaitOne(0)} catch [Threading.AbandonedMutexException] {$locked=$true}
        if(-not $locked) {throw "Test workspace busy: $Name"}
        if(Test-Path -LiteralPath $path) {Clear-BcTestWorkspace $path}
        else {
            New-Item -ItemType Directory -Path $path -Force|Out-Null
            @{owner='nlbc-synthetic-tests-v1';name=$Name}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $path '.nlbc-test-workspace.json') -Encoding UTF8
        }
        $script:BcTestWorkspaceActive[$path]=$true
        return [pscustomobject]@{Path=$path;Gate=$gate}
    } catch {if($locked){$gate.ReleaseMutex()};$gate.Dispose();throw}
}
function Close-BcTestWorkspace($Workspace,[bool]$Passed,[switch]$Retain) {
    try {
        if($Passed -and -not $Retain -and $env:BC_KEEP_TEST_OUTPUT -ne '1') {Clear-BcTestWorkspace $Workspace.Path}
        @{passed=$Passed;retained=(-not $Passed -or $Retain.IsPresent -or $env:BC_KEEP_TEST_OUTPUT -eq '1')}|
            ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Workspace.Path 'last_result.json') -Encoding UTF8
    } finally {
        $script:BcTestWorkspaceActive.Remove($Workspace.Path)
        $Workspace.Gate.ReleaseMutex();$Workspace.Gate.Dispose()
    }
}
