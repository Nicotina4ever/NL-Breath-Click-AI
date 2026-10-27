# SPDX-License-Identifier: MPL-2.0
param([switch]$TryLocked)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/test_workspace.ps1')
function Check($Condition,$Message) {if(-not $Condition){throw $Message}}
if($TryLocked) {
    try {$w=New-BcTestWorkspace 'policy_probe';Close-BcTestWorkspace $w $false;exit 2}
    catch {if([string]$_ -like '*workspace busy*'){exit 0};throw}
}
$w=New-BcTestWorkspace 'policy_probe'
try {
    Set-Content -LiteralPath (Join-Path $w.Path 'synthetic.txt') -Value 'regenerable'
    $rejected=$false
    try {$nested=New-BcTestWorkspace 'policy_probe'} catch {$rejected=$true}
    Check $rejected 'Reentrant acquisition must not reset an active slot'
    $unowned=Join-Path $w.Path 'unowned'
    New-Item -ItemType Directory -Path $unowned|Out-Null
    Set-Content -LiteralPath (Join-Path $unowned 'note.txt') -Value 'preserve'
    $rejected=$false
    try {Clear-BcTestWorkspace $unowned} catch {$rejected=$true}
    Check ($rejected -and (Test-Path -LiteralPath (Join-Path $unowned 'note.txt'))) 'Unmarked content must be left intact'
    $child=Start-Process -WindowStyle Hidden -FilePath powershell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-TryLocked') -PassThru -Wait
    Check ($child.ExitCode -eq 0) 'An active workspace must not be reset by another process'
} finally {Close-BcTestWorkspace $w $false}
Check (Test-Path -LiteralPath (Join-Path $w.Path 'synthetic.txt')) 'Failure must retain evidence'
$w=New-BcTestWorkspace 'policy_probe'
try {
    Check (-not(Test-Path -LiteralPath (Join-Path $w.Path 'synthetic.txt'))) 'Retry must reuse the same slot'
    Set-Content -LiteralPath (Join-Path $w.Path 'synthetic.txt') -Value 'regenerable'
} finally {Close-BcTestWorkspace $w $true}
Check (-not(Test-Path -LiteralPath (Join-Path $w.Path 'synthetic.txt'))) 'Success must release large fixtures'
$rejected=$false
try {Clear-BcTestWorkspace (Split-Path -Parent $w.Path)} catch {$rejected=$true}
Check $rejected 'Managed root itself must never be deleted'
$rejected=$false
try {New-BcTestWorkspace '../escape'} catch {$rejected=$true}
Check $rejected 'Parent traversal must be rejected'
Write-Host 'WORKSPACE PASS: bounded retry, failure retention, success cleanup and path protection'
