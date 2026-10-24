# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  functional_test.ps1 - Verifica che i comandi dell'editor FUNZIONINO
#
#  Il vecchio smoke test controllava che la finestra si aprisse e che i
#  controlli esistessero. Non bastava: i pulsanti erano creati con ID diversi
#  da quelli che onCommand() riconosce, quindi ogni clic cadeva nel `default:`
#  e non succedeva nulla - senza alcun errore, senza alcun sintomo.
#
#  Questo test preme davvero i pulsanti (BM_CLICK) e verifica che il programma
#  reagisca. Se un pulsante non e' collegato, qui si vede.
# ============================================================================

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Ui {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
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

# Gli ID di EditorCommand (editor_commands.h). Se cambiano la', vanno cambiati qui.
$CMD_OPEN      = 1001
$CMD_ANALYZE   = 1200
$CMD_CLEAR_REG = 1202
$CMD_PLAY      = 1500
$CMD_STOP      = 1502
$CMD_SELECTALL = 1105
$BM_CLICK      = 0x00F5
$WM_CLOSE      = 0x0010

$root    = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$editor  = Join-Path $testBin 'bc_editor.exe'
$cli     = Join-Path $testBin 'bc_cli.exe'
$work    = Join-Path $env:TEMP 'bc_functional_test'
$wav     = Join-Path $work 'prova.wav'

if (-not (Test-Path -LiteralPath $editor)) { throw "bc_editor.exe non trovato: $editor" }
if (-not (Test-Path -LiteralPath $cli))    { throw "bc_cli.exe non trovato: $cli" }

New-Item -ItemType Directory -Path $work -Force | Out-Null

$script:pass = 0
$script:fail = 0
function Check($nome, $ok, $dettaglio = '') {
    if ($ok) { $script:pass++; Write-Host ("  [ OK ] " + $nome) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [  X  ] " + $nome + $(if($dettaglio){" -- $dettaglio"})) -ForegroundColor Red }
}

# ---------------------------------------------------------------- segnale
Write-Host ""
Write-Host "Preparo un segnale con respiri e click noti..." -ForegroundColor Cyan
& $cli gen-test $wav --quiet 2>&1 | Out-Null
if (-not (Test-Path -LiteralPath $wav)) { throw "gen-test non ha prodotto $wav" }
Check "segnale di prova generato" $true

# ---------------------------------------------------------------- avvio
Write-Host ""
Write-Host "Avvio l'editor sul file..." -ForegroundColor Cyan
$proc = Start-Process -WindowStyle Normal -FilePath $editor -ArgumentList "`"$wav`"" -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { throw "l'editor e' uscito subito (codice $($proc.ExitCode))" }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { throw "la finestra dell'editor non e' comparsa" }
Start-Sleep -Milliseconds 600
Check "l'editor si avvia e mostra la finestra" $true

$titoloIniziale = [Ui]::Text($hwnd)
Check "il titolo mostra il file aperto" ($titoloIniziale -match 'prova') "titolo: $titoloIniziale"

# ---------------------------------------------------------------- i pulsanti esistono?
Write-Host ""
Write-Host "I pulsanti della barra esistono e sono collegati?" -ForegroundColor Cyan

$attesi = @(
    @{ id = $CMD_OPEN;      nome = 'Apri...' },
    @{ id = $CMD_ANALYZE;   nome = 'Analizza' },
    @{ id = $CMD_PLAY;      nome = 'Riproduci' },
    @{ id = $CMD_STOP;      nome = 'Stop' }
)
foreach ($b in $attesi) {
    $h = [Ui]::GetDlgItem($hwnd, $b.id)
    Check ("pulsante '" + $b.nome + "' presente con l'ID del comando") ($h -ne [IntPtr]::Zero)
}

# ---------------------------------------------------------------- ANALIZZA
Write-Host ""
Write-Host "Premo 'Analizza' e verifico che il programma reagisca..." -ForegroundColor Cyan

$btnAnalizza = [Ui]::GetDlgItem($hwnd, $CMD_ANALYZE)
$primaTitolo = [Ui]::Text($hwnd)

[void][Ui]::SendMessageW($btnAnalizza, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 2500

$proc.Refresh()
Check "l'editor e' ancora vivo dopo 'Analizza'" (-not $proc.HasExited)
$dopoTitolo = [Ui]::Text($hwnd)

# Il documento diventa modificato quando compaiono le regioni: il titolo prende '*'
$cambiato = ($dopoTitolo -ne $primaTitolo) -or ($dopoTitolo -match '\*')
Check "premendo 'Analizza' qualcosa cambia" $cambiato "prima: '$primaTitolo'  dopo: '$dopoTitolo'"

# La barra di stato deve riportare l'esito
$status = [Ui]::GetDlgItem($hwnd, 2109)   # kIdStatus (kIdGainValue e' 2108)
if ($status -ne [IntPtr]::Zero) {
    $testo = [Ui]::Text($status)
    Write-Host "        barra di stato: $testo" -ForegroundColor DarkGray
    Check "la barra di stato riporta l'analisi" ($testo -match 'respir|click|region|rilevat|Nessun')
}

# ---------------------------------------------------------------- SELEZIONA TUTTO
Write-Host ""
Write-Host "Provo la selezione..." -ForegroundColor Cyan
[void][Ui]::PostMessageW($hwnd, 0x0111, [IntPtr]$CMD_SELECTALL, [IntPtr]::Zero)
Start-Sleep -Milliseconds 700
$proc.Refresh()
Check "l'editor sopravvive a 'Seleziona tutto'" (-not $proc.HasExited)
if ($status -ne [IntPtr]::Zero) {
    $testo = [Ui]::Text($status)
    Write-Host "        barra di stato: $testo" -ForegroundColor DarkGray
    Check "la selezione viene riportata" ($testo -match 'elezion|selezion')
}

# ---------------------------------------------------------------- RIPRODUCI / STOP
Write-Host ""
Write-Host "Provo riproduzione e stop..." -ForegroundColor Cyan
$btnPlay = [Ui]::GetDlgItem($hwnd, $CMD_PLAY)
$btnStop = [Ui]::GetDlgItem($hwnd, $CMD_STOP)
[void][Ui]::SendMessageW($btnPlay, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1200
$proc.Refresh()
Check "l'editor sopravvive a 'Riproduci'" (-not $proc.HasExited)
[void][Ui]::SendMessageW($btnStop, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 700
$proc.Refresh()
Check "l'editor sopravvive a 'Stop'" (-not $proc.HasExited)

# ---------------------------------------------------------------- DIMEZZA I FIATI
Write-Host ""
Write-Host "Dimezzamento dei fiati (taglia e richiudi)..." -ForegroundColor Cyan
$CMD_SHORTEN_HALF = 1310

# Prima si analizza di nuovo: le regioni erano state cancellate per la prova
# della selezione.
[void][Ui]::PostMessageW($hwnd, 0x0111, [IntPtr]$CMD_ANALYZE, [IntPtr]::Zero)
Start-Sleep -Milliseconds 2500
$prima = [Ui]::Text($status)
Write-Host "        prima : $prima" -ForegroundColor DarkGray

[void][Ui]::PostMessageW($hwnd, 0x0111, [IntPtr]$CMD_SHORTEN_HALF, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1200
$dopo = [Ui]::Text($status)
Write-Host "        dopo  : $dopo" -ForegroundColor DarkGray

$proc.Refresh()
Check "l'editor sopravvive al dimezzamento" (-not $proc.HasExited)
Check "il dimezzamento riporta l'esito" ($dopo -match 'accorciat|fiati')

# La durata totale del documento deve essere calata: il comando taglia davvero.
$durataPrima = 0.0
$m = [regex]::Match($prima, '(\d+\.\d+) s')
if ($m.Success) { $durataPrima = [double]$m.Groups[1].Value }
$durataDopo = 0.0
$m2 = [regex]::Match($dopo, '(\d+\.\d+) s')
if ($m2.Success) { $durataDopo = [double]$m2.Groups[1].Value }
if ($durataPrima -gt 0 -and $durataDopo -gt 0) {
    Check "il file si e' accorciato" ($durataDopo -lt $durataPrima) ("{0:N3} s -> {1:N3} s" -f $durataPrima, $durataDopo)
}

# ---------------------------------------------------------------- SELEZIONE COL MOUSE
Write-Host ""
Write-Host "Selezione trascinando col mouse sulla forma d''onda..." -ForegroundColor Cyan
$view = [Ui]::GetDlgItem($hwnd, 2000)   # kIdWaveform
Check "la vista della forma d''onda esiste" ($view -ne [IntPtr]::Zero)

if ($view -ne [IntPtr]::Zero) {
    # Prima si cancellano le regioni: dentro una regione il trascinamento la
    # SPOSTA invece di selezionare, ed e'' un comportamento voluto. Per provare
    # la selezione serve una zona libera.
    [void][Ui]::PostMessageW($hwnd, 0x0111, [IntPtr]$CMD_CLEAR_REG, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 600
    $WM_LBUTTONDOWN = 0x0201
    $WM_LBUTTONUP   = 0x0202
    $WM_MOUSEMOVE   = 0x0200
    $MK_LBUTTON     = 0x0001

    # Coordinate dentro la vista: il rettangolo della forma d''onda comincia
    # sotto la barra dei pulsanti, quindi si scende di 60 px.
    $y  = 60
    $x1 = 120
    $x2 = 520
    $lp = [IntPtr](($y -shl 16) -bor $x1)
    [void][Ui]::SendMessageW($view, $WM_LBUTTONDOWN, [IntPtr]1, $lp)
    Start-Sleep -Milliseconds 150
    for ($x = $x1 + 40; $x -le $x2; $x += 40) {
        $m = [IntPtr](($y -shl 16) -bor $x)
        [void][Ui]::SendMessageW($view, $WM_MOUSEMOVE, [IntPtr]$MK_LBUTTON, $m)
        Start-Sleep -Milliseconds 40
    }
    $lp2 = [IntPtr](($y -shl 16) -bor $x2)
    [void][Ui]::SendMessageW($view, $WM_LBUTTONUP, [IntPtr]0, $lp2)
    Start-Sleep -Milliseconds 600

    $proc.Refresh()
    Check "l''editor sopravvive al trascinamento" (-not $proc.HasExited)
    if ($status -ne [IntPtr]::Zero) {
        $testo = [Ui]::Text($status)
        Write-Host "        barra di stato: $testo" -ForegroundColor DarkGray
        Check "il trascinamento crea una selezione" ($testo -match 'elezion')
    }
}

# ---------------------------------------------------------------- chiusura
[void][Ui]::PostMessageW($hwnd, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1200
$proc.Refresh()
if (-not $proc.HasExited) { $proc.Kill(); Check "la finestra si chiude" $false }
else { Check "la finestra si chiude" $true }

# ---------------------------------------------------------------- esito
Write-Host ""
Write-Host ("=" * 62)
if ($script:fail -eq 0) {
    Write-Host "  ESITO: SUPERATO ($script:pass controlli)" -ForegroundColor Green
    exit 0
} else {
    Write-Host "  ESITO: FALLITO ($script:fail falliti su $($script:pass + $script:fail) controlli)" -ForegroundColor Red
    exit 1
}
