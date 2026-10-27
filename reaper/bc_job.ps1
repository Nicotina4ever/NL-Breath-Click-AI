# SPDX-License-Identifier: MPL-2.0
# Background worker: only its own native child is cancellable. No shell evaluation.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Request)
$ErrorActionPreference='Stop'
$jobRoot=Split-Path -Parent ([IO.Path]::GetFullPath($Request))
$result=[ordered]@{status='failed';error='Worker did not complete'}
function Quote-Argument([string]$Value) {
    $out='"'; $slashes=0
    foreach($c in $Value.ToCharArray()) {
        if($c -eq '\') { $slashes++; continue }
        if($c -eq '"') { $out+=('\' * ($slashes*2+1))+'"' }
        else { $out+=('\' * $slashes)+$c }
        $slashes=0
    }
    return $out+('\' * ($slashes*2))+'"'
}
function Run-Engine([string[]]$Arguments,[switch]$ReturnOutput) {
    if(Test-Path -LiteralPath (Join-Path $jobRoot 'cancel')) { throw 'CANCELLED' }
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$job.exe
    $psi.Arguments=($Arguments|ForEach-Object { Quote-Argument $_ }) -join ' '
    $psi.WorkingDirectory=Split-Path -Parent $job.exe
    $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
    $child=New-Object Diagnostics.Process
    $child.StartInfo=$psi
    try {
        if(-not $child.Start()) { throw 'Cannot start engine' }
        $stdout=$child.StandardOutput.ReadToEndAsync(); $stderr=$child.StandardError.ReadToEndAsync()
        $clock=[Diagnostics.Stopwatch]::StartNew()
        while(-not $child.WaitForExit(50)) {
            if(Test-Path -LiteralPath (Join-Path $jobRoot 'cancel')) { $child.Kill(); $child.WaitForExit(); throw 'CANCELLED' }
            if($clock.Elapsed.TotalSeconds -gt 900) { $child.Kill(); $child.WaitForExit(); throw 'Engine timed out' }
        }
        $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        [IO.File]::AppendAllText((Join-Path $jobRoot 'engine.log'),$text,[Text.UTF8Encoding]::new($false))
        if($child.ExitCode -ne 0) { throw ('Engine exit '+$child.ExitCode+': '+$text) }
        if($ReturnOutput) { return $text }
    } finally { $child.Dispose() }
}
try {
    $job=Get-Content -LiteralPath $Request -Raw -Encoding UTF8 | ConvertFrom-Json
    if($null -ne $job.schema -and ($job.schema -ne 1 -or ($job.schema -isnot [int] -and $job.schema -isnot [long]))){throw 'Unsupported job schema'}
    if((Split-Path $job.exe -Leaf) -ne 'bc_studio_cli.exe' -or -not (Test-Path -LiteralPath $job.exe -PathType Leaf)) { throw 'Select bc_studio_cli.exe from a verified build' }
    try {
        $capabilities=Run-Engine -Arguments @('capabilities') -ReturnOutput | ConvertFrom-Json
        if($capabilities.reaper_bridge -ne 1) { throw 'Unsupported bridge protocol' }
        if($job.direct -and $capabilities.direct_editing -ne 2) {throw 'Direct editing requires current engine'}
    } catch {
        if($_.Exception.Message -eq 'CANCELLED') { throw }
        throw 'Motore non compatibile con questa integrazione: seleziona la CLI della build corrente con supporto editing diretto, non la precedente RC2/preview.'
    }
    if($job.mode -eq 'analyze') {
        $segment=Join-Path $jobRoot 'input.wav'; $plan=Join-Path $jobRoot 'analysis.json'
        if($job.prepared) {
            $segment=[string]$job.prepared
            Run-Engine @('verify-source',$job.source,'--sha256',$job.source_sha256)
        } else {
            Run-Engine @('slice',$job.source,'--out',$segment,'--start',[string]$job.offset,'--duration',[string]$job.duration)
        }
        $planArgs=@('plan',$segment,'--out',$plan,'--detector',[string]$job.detector,'--reduction-db',[string]$job.reduction_db,'--fade-ms',[string]$job.fade_ms)
        if($null -ne $job.detect_breath) { $planArgs+=@('--breaths',$(if($job.detect_breath){'1'}else{'0'})) }
        if($null -ne $job.detect_clicks) { $planArgs+=@('--clicks',$(if($job.detect_clicks){'1'}else{'0'})) }
        if($null -ne $job.click_sensitivity) { $planArgs+=@('--click-sensitivity',[string]$job.click_sensitivity) }
        Run-Engine -Arguments $planArgs
        if($job.profile) {
            $filtered=Join-Path $jobRoot 'personal.json'
            Run-Engine @('profile-filter','--kind','breath','--plan',$plan,'--profile',[string]$job.profile,'--out',$filtered)
            $plan=$filtered
        }
        if($job.click_profile) {
            $filtered=Join-Path $jobRoot 'personal_click.json'
            Run-Engine @('profile-filter','--kind','click','--plan',$plan,'--profile',[string]$job.click_profile,'--out',$filtered)
            $plan=$filtered
        }
        $meta=Get-Content -LiteralPath ($segment+'.source.json') -Raw -Encoding UTF8|ConvertFrom-Json
        Run-Engine @('verify-source',$job.source,'--sha256',$meta.source_sha256)
        $result=[ordered]@{status='complete';plan=$plan;prepared=$segment;source_sha256=$meta.source_sha256;frames=$meta.frames}
    } elseif($job.mode -eq 'render' -or $job.mode -eq 'difference') {
        Run-Engine @('verify-source',$job.source,'--sha256',$job.source_sha256)
        Run-Engine @($(if($job.mode -eq 'difference'){'difference'}else{'edit'}),$job.prepared,'--plan',$job.plan,'--out-dir',$jobRoot)
        Run-Engine @('verify-source',$job.source,'--sha256',$job.source_sha256)
        $output=Join-Path $jobRoot (([IO.Path]::GetFileNameWithoutExtension($job.prepared))+$(if($job.mode -eq 'difference'){'_difference.wav'}else{'_edit.wav'}))
        if(-not(Test-Path -LiteralPath ($output+'.edit.json'))) { throw 'Render report missing' }
        $result=[ordered]@{status='complete';output=$output;report=($output+'.edit.json')}
    } elseif($job.mode -eq 'profile-train' -or $job.mode -eq 'labels-export') {
        $arguments=@([string]$job.mode,'--out',[string]$job.output)
        if($job.kind){$arguments+=@('--kind',[string]$job.kind)}
        foreach($planFile in $job.plans) {$arguments+=@('--plan',[string]$planFile)}
        Run-Engine -Arguments $arguments
        $result=[ordered]@{status='complete';output=$job.output}
    } else { throw 'Unknown job mode' }
} catch {
    $result=[ordered]@{status=if($_.Exception.Message -eq 'CANCELLED'){'cancelled'}else{'failed'};error=$_.Exception.Message}
}
$pending=Join-Path $jobRoot 'result.pending'
[IO.File]::WriteAllText($pending,($result|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
[IO.File]::Move($pending,(Join-Path $jobRoot 'result.json'))
