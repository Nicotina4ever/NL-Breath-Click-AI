# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  detection_settings_test.ps1 — verifica GUI reale delle impostazioni.
#
#  Controlla: anteprima locale che si aggiorna, OK che salva, riavvio che
#  ricarica, Annulla che non modifica. Usa una cartella APPDATA temporanea per
#  non toccare le impostazioni reali dell'utente.
# ============================================================================
param([string]$AudioFixture='')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'test_window.ps1')
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class DsGui {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
}
"@
$root = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$studio = Join-Path $testBin 'bc_studio.exe'
$gen = Join-Path $testBin 'bc_cli.exe'
if (-not (Test-Path $studio) -or -not (Test-Path $gen)) { throw 'build non trovata' }
$work = Join-Path $env:TEMP ('bc_settings_test_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
$wav = Join-Path $work 'fixture.wav'
if ($AudioFixture) {
    $fixturePath=(Resolve-Path -LiteralPath $AudioFixture).Path
    $buildPrefix=[IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
    if(-not $fixturePath.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Only build-local audio copies are allowed'}
    Copy-Item -LiteralPath $fixturePath -Destination $wav
} else {
    & $gen gen-test $wav --quiet 2>&1 | Out-Null
    if (-not (Test-Path $wav)) { throw 'WAV di prova non generato' }
}

$oldAppData = $env:APPDATA
$env:APPDATA = $work
$settingsFile = Join-Path $work 'BreathClickStudio\detector_settings.ini'
$studioProcess = $null
$failures = 0
function Check($condition, $message) {
    if ($condition) { Write-Host "  [ OK ] $message" }
    else { Write-Host "  [ X  ] $message"; $script:failures++ }
}
function FindDialog($pidValue) {
    $found = [IntPtr]::Zero
    $callback = [DsGui+EnumProc]{
        param($h, $unused)
        $id = 0
        [void][DsGui]::GetWindowThreadProcessId($h, [ref]$id)
        if ($id -eq $pidValue) {
            $class = New-Object Text.StringBuilder 128
            [void][DsGui]::GetClassNameW($h, $class, 128)
            if ($class.ToString() -eq 'BreathClickSettingsDialog') { $script:foundSettings = $h; return $false }
        }
        return $true
    }
    [void][DsGui]::EnumWindows($callback, [IntPtr]::Zero)
    if ($script:foundSettings -ne [IntPtr]::Zero) { return $script:foundSettings }
    return [IntPtr]::Zero
}
function StartStudio() {
    $proc = Start-Process -WindowStyle Normal -FilePath $studio -ArgumentList @("`"$wav`"") -PassThru
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 200
        $proc.Refresh()
        if ($proc.HasExited) { throw "studio terminato ($($proc.ExitCode))" }
        $window=[BcTestWindow]::Find($proc.Id)
        if ($window -ne [IntPtr]::Zero) {
            # Generated noise is a DSP fixture, not labelled Respiro-en speech.
            # Real-recording mode deliberately exercises the normal Fusion default.
            if(-not $AudioFixture) {
                $method=[DsGui]::GetDlgItem($window,2559)
                [void][DsGui]::SendMessageW($method,0x14e,[IntPtr]2,[IntPtr]0)
                [void][DsGui]::SendMessageW($window,0x111,[IntPtr](2559 -bor (1 -shl 16)),[IntPtr]0)
            }
            return $proc
        }
    }
    throw 'finestra studio non comparsa'
}
function OpenSettings($proc) {
    $script:foundSettings = [IntPtr]::Zero
    [void][DsGui]::PostMessageW(([BcTestWindow]::Find($proc.Id)), 0x0111, [IntPtr]1201, [IntPtr]0)
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 200
        $dialog = FindDialog $proc.Id
        if ($dialog -ne [IntPtr]::Zero) { return $dialog }
    }
    throw 'finestra impostazioni non comparsa'
}

try {
    $studioProcess = StartStudio
    $dialog = OpenSettings $studioProcess
    Check ($dialog -ne [IntPtr]::Zero) 'finestra impostazioni aperta'
    $slider = [DsGui]::GetDlgItem($dialog, 3000)
    $preview = [DsGui]::GetDlgItem($dialog, 3019)
    [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]350)  # TBM_SETPOS
    [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)        # WM_HSCROLL
    Start-Sleep -Milliseconds 1200
    $text = New-Object Text.StringBuilder 512
    [void][DsGui]::GetWindowTextW($preview, $text, 512)
    $previewBefore = $text.ToString()
    Check ($previewBefore -like '*Prima:*Dopo:*') 'anteprima locale mostra prima/dopo'
    # Rapid changes must not run analysis on the window thread. Only the final
    # generation may be displayed; presets/defaults must also schedule a job.
    $timer = [Diagnostics.Stopwatch]::StartNew()
    for ($n = 0; $n -lt 100; $n++) {
        [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr](100 + $n))
        [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)
    }
    $timer.Stop()
    Check ($timer.ElapsedMilliseconds -lt 1500) "100 cambi senza blocco GUI ($($timer.ElapsedMilliseconds) ms)"
    [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]350)
    [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)
    Start-Sleep -Milliseconds 700
    $latest = New-Object Text.StringBuilder 512
    [void][DsGui]::GetWindowTextW($preview, $latest, 512)
    Check ($latest.ToString() -eq $previewBefore) 'solo l ultima generazione aggiorna il risultato'

    [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]900)
    [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)
    Start-Sleep -Milliseconds 1300
    $text2 = New-Object Text.StringBuilder 512
    [void][DsGui]::GetWindowTextW($preview, $text2, 512)
    Check ($text2.ToString() -ne $previewBefore) 'la soglia aggiorna i conteggi prima/dopo'
    [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]350)
    [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)
    Start-Sleep -Milliseconds 400

    $breaths=[DsGui]::GetDlgItem($dialog,3060)
    $clicks=[DsGui]::GetDlgItem($dialog,3061)
    Check ($breaths -ne [IntPtr]::Zero -and $clicks -ne [IntPtr]::Zero) 'independent breath/click checkboxes exist'
    [void][DsGui]::SendMessageW($breaths,0xf1,[IntPtr]0,[IntPtr]0)
    [void][DsGui]::SendMessageW($clicks,0xf1,[IntPtr]1,[IntPtr]0)
    [void][DsGui]::SendMessageW($dialog,0x111,[IntPtr]3061,[IntPtr]0)
    # Applica conferma senza chiudere: il file viene aggiornato, il dialogo resta.
    [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]500)
    [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)
    Start-Sleep -Milliseconds 250
    [void][DsGui]::PostMessageW($dialog, 0x0111, [IntPtr]3010, [IntPtr]0)   # Applica
    Start-Sleep -Milliseconds 500
    $applyText = Get-Content $settingsFile -Raw
    $applyMatch = [regex]::Match($applyText, 'breathThreshold=([0-9.]+)')
    $applyValue = if ($applyMatch.Success) { [double]::Parse($applyMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) } else { -1 }
    Check ([math]::Abs($applyValue - 0.5) -lt 0.001) "Applica salva senza chiudere ($applyValue)"
    Check ((Get-Process -Id $studioProcess.Id -ErrorAction SilentlyContinue) -ne $null) 'Applica lascia aperto il dialogo'
    [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]350)
    [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)
    Start-Sleep -Milliseconds 300
    [void][DsGui]::PostMessageW($dialog, 0x0111, [IntPtr]3008, [IntPtr]0)   # OK
    Start-Sleep -Milliseconds 700
    Check (Test-Path $settingsFile) 'OK ha scritto le impostazioni persistenti'
    if (Test-Path $settingsFile) {
        $content = Get-Content $settingsFile -Raw
        $match = [regex]::Match($content, 'breathThreshold=([0-9.]+)')
        $saved = if ($match.Success) { [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) } else { -1 }
        Check ([math]::Abs($saved - 0.35) -lt 0.001) "soglia salvata ($saved)"
    }
    $regionsAfterSettings = [int][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)), 0x812D, [IntPtr]0, [IntPtr]0)
    Check ($regionsAfterSettings -eq 0) 'anteprima e OK non modificano il progetto'
    [void][DsGui]::PostMessageW(([BcTestWindow]::Find($studioProcess.Id)), 0x0010, [IntPtr]0, [IntPtr]0)
    Start-Sleep -Milliseconds 700
    if (-not $studioProcess.HasExited) { Stop-Process -Id $studioProcess.Id -Force }

    # Riavvio: il valore confermato deve tornare.
    $studioProcess = StartStudio
    $dialog = OpenSettings $studioProcess
    $slider = [DsGui]::GetDlgItem($dialog, 3000)
    $position = [int][DsGui]::SendMessageW($slider, 0x0400, [IntPtr]0, [IntPtr]0)  # TBM_GETPOS
    Check ([math]::Abs($position - 350) -le 2) "soglia ricaricata dopo riavvio (pos=$position)"

    Check (([int][DsGui]::SendMessageW([DsGui]::GetDlgItem($dialog,3060),0xf0,[IntPtr]0,[IntPtr]0)) -eq 0) 'breath OFF persisted after restart'
    Check (([int][DsGui]::SendMessageW([DsGui]::GetDlgItem($dialog,3061),0xf0,[IntPtr]0,[IntPtr]0)) -eq 1) 'click ON persisted after restart'
    # Annulla: cambio temporaneo, nessuna scrittura.
    [void][DsGui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]700)
    [void][DsGui]::SendMessageW($dialog, 0x0114, [IntPtr]0, $slider)
    Start-Sleep -Milliseconds 250
    [void][DsGui]::PostMessageW($dialog, 0x0111, [IntPtr]3009, [IntPtr]0)   # Annulla
    Start-Sleep -Milliseconds 700
    if (Test-Path $settingsFile) {
        $content = Get-Content $settingsFile -Raw
        $match = [regex]::Match($content, 'breathThreshold=([0-9.]+)')
        $saved = if ($match.Success) { [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) } else { -1 }
        Check ([math]::Abs($saved - 0.35) -lt 0.001) 'Annulla non ha modificato il file'
    }
    [void][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)),0x111,[IntPtr]1200,[IntPtr]0)
    $regionCount=[int][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)),0x812d,[IntPtr]0,[IntPtr]0)
    Check ($regionCount -gt 0) 'click-only GUI analysis produces candidates'
    for($r=0;$r -lt $regionCount;$r++) {
        Check (([int][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)),0x8145,[IntPtr]$r,[IntPtr]0)) -eq 1) 'actual GUI analysis includes only click categories'
    }
    foreach($mode in @('breaths','both')) {
        # Fresh proposals remove prior detections; human decisions are tested in
        # the RC2 workflow regression and are intentionally not erased here.
        [void][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)),0x111,[IntPtr]1202,[IntPtr]0)
        $dialog=OpenSettings $studioProcess
        [void][DsGui]::SendMessageW([DsGui]::GetDlgItem($dialog,3060),0xf1,[IntPtr]1,[IntPtr]0)
        $clickFlag=if($mode -eq 'both'){1}else{0}
        [void][DsGui]::SendMessageW([DsGui]::GetDlgItem($dialog,3061),0xf1,[IntPtr]$clickFlag,[IntPtr]0)
        [void][DsGui]::PostMessageW($dialog,0x111,[IntPtr]3008,[IntPtr]0)
        Start-Sleep -Milliseconds 400
        [void][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)),0x111,[IntPtr]1200,[IntPtr]0)
        $count=[int][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)),0x812d,[IntPtr]0,[IntPtr]0)
        $nb=0;$nc=0
        for($r=0;$r -lt $count;$r++) {
            $kind=[int][DsGui]::SendMessageW(([BcTestWindow]::Find($studioProcess.Id)),0x8145,[IntPtr]$r,[IntPtr]0)
            if($kind -eq 0){$nb++}else{$nc++}
        }
        Check ($nb -gt 0 -and (($mode -eq 'both' -and $nc -gt 0) -or ($mode -eq 'breaths' -and $nc -eq 0))) "actual standalone analysis $mode : breaths=$nb clicks=$nc"
    }
} finally {
    if ($studioProcess -and -not $studioProcess.HasExited) {
        Stop-Process -Id $studioProcess.Id -Force
    }
    $env:APPDATA = $oldAppData
    if (Test-Path $work) { Remove-Item -Recurse -Force $work }
}
if ($failures -ne 0) { Write-Host "SETTINGS TEST: $failures falliti"; exit 1 }
Write-Host 'SETTINGS TEST: OK'
