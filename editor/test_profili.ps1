# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  test_profili.ps1 - I profili di modello funzionano?
#
#  Un profilo e' un assetto di rilevamento salvato con un nome. Il profilo
#  "base" deve esserci sempre; selezionandone un altro le soglie cambiano e la
#  barra di stato lo deve dire, altrimenti non si capisce perche' due analisi
#  dello stesso file danno risultati diversi.
# ============================================================================

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Pr {
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

$kIdStatus   = 2109
$kModelBase  = 1910
$WM_COMMAND  = 0x0111
$WM_CLOSE    = 0x0010

$root   = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$editor = Join-Path $testBin 'bc_editor.exe'
$cli    = Join-Path $testBin 'bc_cli.exe'
$wav    = Join-Path $env:TEMP 'bc_profili.wav'
& $cli gen-test $wav --quiet 2>&1 | Out-Null

$proc = Start-Process -WindowStyle Normal -FilePath $editor -ArgumentList "`"$wav`"" -PassThru
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
Write-Host "Profili di modello" -ForegroundColor Cyan

$status = [Pr]::GetDlgItem($hwnd, $kIdStatus)
Check "la barra di stato esiste" ($status -ne [IntPtr]::Zero)

$testo1 = [Pr]::Text($status)
Write-Host "        stato: $testo1" -ForegroundColor DarkGray
Check "il profilo attivo e' mostrato" ($testo1 -match 'profilo') "stato: '$testo1'"
Check "all'avvio si parte dal profilo base" ($testo1 -match 'profilo: base') "stato: '$testo1'"

# --- selezione di un altro profilo -----------------------------------------
# 1910 e' il primo profilo dell'elenco, 1911 il secondo. Ma l'elenco cambia
# appena si aggiunge un profilo, quindi il test non deve sapere quale sia il
# secondo: deve solo verificare che il cambio avvenga e che il nome mostrato
# sia uno di quelli realmente presenti.
$cartellaModelli = Join-Path $testBin 'models'
$disponibili = @(Get-ChildItem $cartellaModelli -Directory -ErrorAction SilentlyContinue |
                 ForEach-Object { $_.Name })
Write-Host "        profili trovati: $($disponibili -join ', ')" -ForegroundColor DarkGray

[void][Pr]::PostMessageW($hwnd, $WM_COMMAND, [IntPtr]($kModelBase + 1), [IntPtr]::Zero)
Start-Sleep -Milliseconds 600
$proc.Refresh()
Check "l'editor sopravvive al cambio profilo" (-not $proc.HasExited)

$testo2 = [Pr]::Text($status)
Write-Host "        stato: $testo2" -ForegroundColor DarkGray

# Il profilo attivo e' quello che compare dopo "profilo: ".
$attivo = if ($testo2 -match 'profilo:\s*([\w\-]+)') { $Matches[1] } else { '' }
Check "il profilo e' cambiato" ($testo2 -ne $testo1) "prima: '$testo1' dopo: '$testo2'"
Check "il profilo mostrato esiste davvero" ($disponibili -contains $attivo) "profilo: '$attivo'"

# --- ritorno al base -------------------------------------------------------
[void][Pr]::PostMessageW($hwnd, $WM_COMMAND, [IntPtr]($kModelBase), [IntPtr]::Zero)
Start-Sleep -Milliseconds 600
$testo3 = [Pr]::Text($status)
Write-Host "        stato: $testo3" -ForegroundColor DarkGray
Check "tornando al base il profilo torna quello di partenza" ($testo3 -match 'profilo: base') "stato: '$testo3'"

# --- chiusura --------------------------------------------------------------
[void][Pr]::PostMessageW($hwnd, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
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
