# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  smoke_test.ps1 - Prova di avvio dell'editor
#
#  Un'interfaccia grafica non si presta ai test automatici, ma il modo piu'
#  comune in cui un editor fallisce e' non avviarsi affatto: una risorsa non
#  creata, un controllo con un identificatore sbagliato, un crash in WM_CREATE.
#  Questa prova lo intercetta: genera un WAV, avvia l'editor su quel file,
#  aspetta e verifica che sia ancora vivo e che abbia aperto la finestra.
#
#  Uso: smoke_test.ps1 <bc_editor.exe> <bc_cli.exe>
# ============================================================================
param(
    [Parameter(Mandatory = $true)][string]$Editor,
    [Parameter(Mandatory = $true)][string]$Cli
)

$ErrorActionPreference = 'Stop'

$workDir = Split-Path -Parent $Editor
$testWav = Join-Path $workDir 'editor_smoke.wav'

Write-Host "Prova di avvio dell'editor"
Write-Host "=========================="
Write-Host "Editor: $Editor"

# --- materiale di prova ----------------------------------------------------
if (-not (Test-Path -LiteralPath $testWav)) {
    Write-Host "Genero il file di prova..."
    & $Cli gen-test $testWav --quiet | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $testWav)) {
        Write-Host "FALLITO: impossibile generare il file di prova"
        exit 1
    }
}
Write-Host "File di prova: $testWav ($([math]::Round((Get-Item $testWav).Length / 1KB)) KB)"

# --- avvio -----------------------------------------------------------------
$process = Start-Process -FilePath $Editor -ArgumentList "`"$testWav`"" -PassThru
Write-Host "Avviato con PID $($process.Id)"

$failures = 0

# Attesa breve: se c'e' un crash all'avvio si manifesta subito.
Start-Sleep -Seconds 3
$process.Refresh()

if ($process.HasExited) {
    Write-Host "FALLITO: l'editor e' uscito subito con codice $($process.ExitCode)"
    exit 1
}
Write-Host "  [ OK ] l'editor e' ancora in esecuzione dopo l'avvio"

# La finestra principale deve esistere e avere un titolo.
$title = $process.MainWindowTitle
if ([string]::IsNullOrWhiteSpace($title)) {
    Write-Host "FALLITO: nessuna finestra principale"
    $failures++
} else {
    Write-Host "  [ OK ] finestra principale: '$title'"
}

# Il titolo deve contenere il nome del file aperto: se non lo contiene, il
# caricamento non e' andato a buon fine.
if ($title -notlike '*editor_smoke*') {
    Write-Host "FALLITO: il titolo non riporta il file aperto (caricamento fallito?)"
    $failures++
} else {
    Write-Host "  [ OK ] il file risulta aperto"
}

# --- controlli strutturali -------------------------------------------------
# Non basta che la finestra esista: un difetto reale e' passato proprio perche'
# la prova si fermava qui. I controlli venivano creati con padre nullo -- quindi
# come finestre di primo livello vaganti -- e la finestra appariva vuota pur
# avendo il titolo corretto.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Probe {
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr hWnd);
}
"@

$children = 0
$child = [Win32Probe]::GetWindow($process.MainWindowHandle, 5)   # GW_CHILD
if ($child -eq [IntPtr]::Zero) {
    Write-Host "FALLITO: la finestra non ha controlli figli."
    Write-Host "         Sintomo tipico: window_ assegnato dopo WM_CREATE, quindi i"
    Write-Host "         controlli sono nati come finestre di primo livello."
    $failures++
} else {
    $probe = $child
    while ($probe -ne [IntPtr]::Zero -and $children -lt 500) {
        $children++
        $probe = [Win32Probe]::GetWindow($probe, 2)   # GW_HWNDNEXT
    }
    Write-Host "  [ OK ] la finestra contiene $children controlli figli"
}

if ([Win32Probe]::GetMenu($process.MainWindowHandle) -eq [IntPtr]::Zero) {
    Write-Host "FALLITO: la finestra non ha una barra dei menu"
    $failures++
} else {
    Write-Host "  [ OK ] barra dei menu presente"
}

# --- chiusura --------------------------------------------------------------
Write-Host "Chiusura..."
$null = $process.CloseMainWindow()
Start-Sleep -Seconds 3
$process.Refresh()

if (-not $process.HasExited) {
    Write-Host "  [ !! ] la finestra non si e' chiusa da sola: terminazione forzata"
    $process.Kill()
    Start-Sleep -Seconds 1
} else {
    Write-Host "  [ OK ] chiusura pulita dalla finestra"
}

Remove-Item -LiteralPath $testWav -ErrorAction SilentlyContinue

Write-Host ""
if ($failures -eq 0) {
    Write-Host "PROVA SUPERATA"
    exit 0
}
Write-Host "PROVE FALLITE: $failures"
exit 1
