# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  test_guadagno.ps1 - Il cursore del guadagno fa qualcosa?
#
#  L'utente dice: "nella standalone guadagno non funziona".
#  Il documento applica il guadagno (gainCurve), quindi il sospetto e' nel
#  percorso del messaggio: il cursore muove, ma la finestra non lo sa.
#
#  Questo test muove il cursore come farebbe l'utente (TBM_SETPOS + WM_HSCROLL)
#  e controlla se l'etichetta cambia e se il documento cambia.
# ============================================================================

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'test_window.ps1')

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Gu {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
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

$kIdGain      = 2107
$kIdGainValue = 2108
$kIdStatus    = 2109
$TBM_SETPOS   = 0x0405
$TBM_GETPOS   = 0x0400
$WM_HSCROLL   = 0x0114
$WM_CLOSE     = 0x0010
$TB_LINEUP    = 0

$root   = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$editor = Join-Path $testBin 'bc_editor.exe'
$cli    = Join-Path $testBin 'bc_cli.exe'
$wav    = Join-Path $env:TEMP 'bc_gain_test.wav'

& $cli gen-test $wav --quiet 2>&1 | Out-Null

$proc = Start-Process -WindowStyle Normal -FilePath $editor -ArgumentList "`"$wav`"" -PassThru
$hwnd = [IntPtr]::Zero
$hwnd = Wait-BcEditorWindow -Process $proc
Start-Sleep -Milliseconds 800

$script:pass = 0
$script:fail = 0
function Check($nome, $ok, $dettaglio = '') {
    if ($ok) { $script:pass++; Write-Host ("  [ OK ] " + $nome) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [  X  ] " + $nome + $(if($dettaglio){" -- $dettaglio"})) -ForegroundColor Red }
}

Write-Host ""
Write-Host "Cursore del guadagno" -ForegroundColor Cyan

$slider = [Gu]::GetDlgItem($hwnd, $kIdGain)
$status = [Gu]::GetDlgItem($hwnd, $kIdStatus)

# L'etichetta NON ha un ID: e'' creata con nullptr come hMenu, quindi
# GetDlgItem non puo'' trovarla. Si cerca fra i figli quella che contiene "dB".
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class Figli {
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    public const uint GW_CHILD = 5;
    public const uint GW_HWNDNEXT = 2;
    public static List<IntPtr> Elenco(IntPtr padre) {
        var lista = new List<IntPtr>();
        IntPtr f = GetWindow(padre, GW_CHILD);
        while (f != IntPtr.Zero) { lista.Add(f); f = GetWindow(f, GW_HWNDNEXT); }
        return lista;
    }
}
"@

$label = [IntPtr]::Zero
foreach ($figlio in [Figli]::Elenco($hwnd)) {
    $testo = [Gu]::Text($figlio)
    if ($testo -match 'dB') { $label = $figlio; break }
}
Write-Host "        figli trovati: $(([Figli]::Elenco($hwnd)).Count)" -ForegroundColor DarkGray

Check "il cursore esiste (ID $kIdGain)" ($slider -ne [IntPtr]::Zero)
Check "l'etichetta del guadagno esiste fra i figli" ($label -ne [IntPtr]::Zero)

$etichettaIniziale = [Gu]::Text($label)
Write-Host "        etichetta iniziale: $etichettaIniziale" -ForegroundColor DarkGray

# --- 1. Spostamento via TBM_SETPOS + WM_HSCROLL (come trascinando) ---------
[void][Gu]::SendMessageW($slider, $TBM_SETPOS, [IntPtr]1, [IntPtr]120)
[void][Gu]::SendMessageW($hwnd, $WM_HSCROLL, [IntPtr]0, $slider)
Start-Sleep -Milliseconds 500

$posizione = [Gu]::SendMessageW($slider, $TBM_GETPOS, [IntPtr]0, [IntPtr]::Zero)
$etichettaDopo = [Gu]::Text($label)
Write-Host "        posizione cursore: $($posizione.ToInt32())" -ForegroundColor DarkGray
Write-Host "        etichetta dopo   : $etichettaDopo" -ForegroundColor DarkGray

Check "il cursore si e' spostato" ($posizione.ToInt32() -eq 120) "posizione: $($posizione.ToInt32())"
Check "l'etichetta del guadagno si aggiorna" ($etichettaDopo -ne $etichettaIniziale) "era '$etichettaIniziale', ora '$etichettaDopo'"
Check "l'etichetta mostra -12.0 dB" ($etichettaDopo -match '-12') "etichetta: '$etichettaDopo'"

# --- 2. Un secondo spostamento, per escludere che fosse un caso -------------
[void][Gu]::SendMessageW($slider, $TBM_SETPOS, [IntPtr]1, [IntPtr]60)
[void][Gu]::SendMessageW($hwnd, $WM_HSCROLL, [IntPtr]0, $slider)
Start-Sleep -Milliseconds 400
$etichetta2 = [Gu]::Text($label)
Write-Host "        etichetta dopo   : $etichetta2" -ForegroundColor DarkGray
Check "l'etichetta segue il secondo spostamento" ($etichetta2 -match '-6') "etichetta: '$etichetta2'"

# --- 3. Il documento risulta modificato? -----------------------------------
$titolo = [Gu]::Text($hwnd)
Check "il documento risulta modificato (titolo con *)" ($titolo -match '\*') "titolo: '$titolo'"

$proc.Refresh()
Check "l'editor sopravvive" (-not $proc.HasExited)

[void][Gu]::PostMessageW($hwnd, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1000
$proc.Refresh()
if (-not $proc.HasExited) { $proc.Kill() }

Write-Host ""
Write-Host ("=" * 62)
if ($script:fail -eq 0) {
    Write-Host "  ESITO: SUPERATO ($script:pass controlli)" -ForegroundColor Green
    exit 0
} else {
    Write-Host "  ESITO: FALLITO ($script:fail su $($script:pass + $script:fail))" -ForegroundColor Red
    exit 1
}
