# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  test_tracce.ps1 - La multi-traccia funziona?
#
#  L'editor accetta piu' file: ognuno diventa una traccia. Il dialogo di
#  apertura non e' pilotabile da un test, quindi il test passa i file da riga
#  di comando ed e' proprio quel percorso a essere verificato.
#
#  Si controlla:
#    - il titolo dice quante tracce ci sono e quale e' attiva
#    - cambiando traccia il titolo cambia
#    - il cambio non azzera il documento (la barra di stato mostra il file)
#    - l'editor sopravvive
# ============================================================================

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Tr {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowTextLengthW(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static string Text(IntPtr h) {
        int n = GetWindowTextLengthW(h);
        var sb = new StringBuilder(n + 2);
        GetWindowTextW(h, sb, sb.Capacity);
        return sb.ToString();
    }
}
"@

$kIdStatus      = 2109
$kTrackSwitch   = 1800
$WM_COMMAND     = 0x0111
$WM_CLOSE       = 0x0010

$root   = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$editor = Join-Path $testBin 'bc_editor.exe'
$cli    = Join-Path $testBin 'bc_cli.exe'
$wa     = Join-Path $env:TEMP 'bc_track_a.wav'
$wb     = Join-Path $env:TEMP 'bc_track_b.wav'

& $cli gen-test $wa --quiet 2>&1 | Out-Null
& $cli gen-test $wb --quiet 2>&1 | Out-Null

# Due file, due tracce.
$proc = Start-Process -WindowStyle Normal -FilePath $editor -ArgumentList "`"$wa`" `"$wb`"" -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { throw "l'editor e' uscito subito" }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { throw "finestra non trovata" }
Start-Sleep -Milliseconds 900

$script:pass = 0
$script:fail = 0
function Check($nome, $ok, $dettaglio = '') {
    if ($ok) { $script:pass++; Write-Host ("  [ OK ] " + $nome) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [  X  ] " + $nome + $(if($dettaglio){" -- $dettaglio"})) -ForegroundColor Red }
}

Write-Host ""
Write-Host "Tracce multiple" -ForegroundColor Cyan

$titolo1 = [Tr]::Text($hwnd)
Write-Host "        titolo: $titolo1" -ForegroundColor DarkGray

Check "due file aperti come due tracce" ($titolo1 -match 'traccia 2 di 2') "titolo: '$titolo1'"

$status = [Tr]::GetDlgItem($hwnd, $kIdStatus)
$testo1 = if ($status -ne [IntPtr]::Zero) { [Tr]::Text($status) } else { '' }
Write-Host "        stato : $testo1" -ForegroundColor DarkGray
Check "la barra di stato mostra un file" ($testo1 -match '\.wav') "stato: '$testo1'"

# --- cambio traccia --------------------------------------------------------
# Si parte dalla 2 (l'ultima aperta) e si va alla 1.
[void][Tr]::PostMessageW($hwnd, $WM_COMMAND, [IntPtr]$kTrackSwitch, [IntPtr]::Zero)
Start-Sleep -Milliseconds 700
$proc.Refresh()
Check "l'editor sopravvive al cambio traccia" (-not $proc.HasExited)

$titolo2 = [Tr]::Text($hwnd)
Write-Host "        titolo: $titolo2" -ForegroundColor DarkGray
Check "il titolo dice che e' la prima traccia" ($titolo2 -match 'traccia 1 di 2') "titolo: '$titolo2'"
Check "il titolo e' cambiato" ($titolo2 -ne $titolo1) "prima: '$titolo1' dopo: '$titolo2'"

$testo2 = if ($status -ne [IntPtr]::Zero) { [Tr]::Text($status) } else { '' }
Write-Host "        stato : $testo2" -ForegroundColor DarkGray
Check "la traccia ha un documento caricato" ($testo2 -match '\.wav') "stato: '$testo2'"

# --- ritorno alla seconda --------------------------------------------------
[void][Tr]::PostMessageW($hwnd, $WM_COMMAND, [IntPtr]($kTrackSwitch + 1), [IntPtr]::Zero)
Start-Sleep -Milliseconds 700
$titolo3 = [Tr]::Text($hwnd)
Write-Host "        titolo: $titolo3" -ForegroundColor DarkGray
Check "tornando alla seconda il titolo lo dice" ($titolo3 -match 'traccia 2 di 2') "titolo: '$titolo3'"

# --- chiusura --------------------------------------------------------------
[void][Tr]::PostMessageW($hwnd, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1200
$proc.Refresh()
if (-not $proc.HasExited) { $proc.Kill(); Check "la finestra si chiude" $false }
else { Check "la finestra si chiude" $true }

Write-Host ""
Write-Host ("=" * 62)
if ($script:fail -eq 0) {
    Write-Host "  ESITO: SUPERATO ($script:pass controlli)" -ForegroundColor Green
    exit 0
} else {
    Write-Host "  ESITO: FALLITO ($script:fail su $($script:pass + $script:fail))" -ForegroundColor Red
    exit 1
}
