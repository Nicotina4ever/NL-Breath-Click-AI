# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  gui_interaction_test.ps1 — collaudo della GUI con mouse e tastiera reali.
#
#  Apre una sessione con due respiri noti, clicca una regione, trascina centro
#  e bordi, esclude/reinclude, cambia modalita', usa zoom e anteprima. Ogni
#  controllo legge il pannello contestuale e lo stato della finestra: se il
#  modello dati non cambia, il test fallisce anche se il motore e' corretto.
# ============================================================================
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$testBin = if ($env:BC_TEST_BIN) { $env:BC_TEST_BIN } else { Join-Path $root 'build\bin\Release' }
$studio = Join-Path $testBin 'bc_studio.exe'
$gen = Join-Path $testBin 'bc_cli.exe'
$work = Join-Path $env:TEMP 'bc_gui_interaction_test'
if (Test-Path -LiteralPath $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null

if (-not (Test-Path -LiteralPath $studio)) { throw "bc_studio.exe non trovato: $studio" }
if (-not (Test-Path -LiteralPath $gen)) { throw "bc_cli.exe non trovato: $gen" }

$wav = Join-Path $work 'fixture.wav'
& $gen gen-test $wav --quiet 2>&1 | Out-Null
if (-not (Test-Path -LiteralPath $wav)) { throw 'WAV di prova non generato' }
function FileHash($path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}
$hashBefore = FileHash $wav

# Due regioni ampie 1 s: abbastanza larghe per trascinare centro e bordi.
$session = [ordered]@{
    schema = 2
    source = $wav
    source_relative = ''
    reduction_db = -12.0
    fade_ms = 15.0
    master_gain_db = 0.0
    regions = @(
        [ordered]@{ kind = 0; start = 48000; end = 96000; reduction_db = -12.0;
                    enabled = $true; approved = $true; from_detection = $true;
                    edit_mode = 0; keep_fraction = 0.5; fade_ms = 15.0;
                    safety_margin_ms = 0.0; decision_source = 'automatic';
                    classification = 'dsp'; training_eligible = $false },
        [ordered]@{ kind = 0; start = 144000; end = 192000; reduction_db = -12.0;
                    enabled = $true; approved = $true; from_detection = $true;
                    edit_mode = 0; keep_fraction = 0.5; fade_ms = 15.0;
                    safety_margin_ms = 0.0; decision_source = 'automatic';
                    classification = 'dsp'; training_eligible = $false }
    )
    corrections = @()
    events = @()
}
$sessionPath = Join-Path $work 'fixture.bcnstudio.json'
$session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sessionPath -Encoding UTF8

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Gui {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetFocus();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
}
"@

Add-Type -AssemblyName System.Drawing

function CaptureViewPng($view, $path) {
    $rect = New-Object Gui+RECT
    [void][Gui]::GetWindowRect($view, [ref]$rect)
    $width = [Math]::Max(1, $rect.right - $rect.left)
    $height = [Math]::Max(1, $rect.bottom - $rect.top)
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $hdc = $graphics.GetHdc()
        try {
            [void][Gui]::PrintWindow($view, $hdc, 1)
        } finally {
            $graphics.ReleaseHdc($hdc)
        }
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function CountBrightPixels($path) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($path)
    $count = 0
    try {
        for ($y = 0; $y -lt $bitmap.Height; $y += 2) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 2) {
                $color = $bitmap.GetPixel($x, $y)
                if ($color.R -ge 225 -and $color.G -ge 225 -and $color.B -ge 225) { $count++ }
            }
        }
    } finally {
        $bitmap.Dispose()
    }
    return $count
}

$script:pass = 0
$script:fail = 0
function Check($name, $ok, $detail = '') {
    if ($ok) { $script:pass++; Write-Host ("  [ OK ] " + $name) -ForegroundColor Green }
    else { $script:fail++; Write-Host ("  [  X  ] " + $name + $(if ($detail) { " -- $detail" })) -ForegroundColor Red }
}
function TextOf($hwnd) {
    $sb = New-Object System.Text.StringBuilder 512
    [void][Gui]::GetWindowTextW($hwnd, $sb, 512)
    return $sb.ToString()
}
function PanelTexts($panel) {
    $items = New-Object System.Collections.ArrayList
    $callback = [Gui+EnumProc]{
        param($hwnd, $unused)
        $text = TextOf $hwnd
        if ($text.Length -gt 0) { [void]$items.Add($text) }
        return $true
    }
    [void][Gui]::EnumChildWindows($panel, $callback, [IntPtr]::Zero)
    return $items
}
function PanelHas($panel, $needle) {
    foreach ($text in (PanelTexts $panel)) { if ($text -like "*$needle*") { return $true } }
    return $false
}
function PanelValue($panel, $id) { return TextOf ([Gui]::GetDlgItem($panel, $id)) }
function Seconds($text) {
    $value = 0.0
    $clean = ($text -replace 's$','').Trim() -replace ',','.'
    $invariant = [System.Globalization.CultureInfo]::InvariantCulture
    if ([double]::TryParse($clean, [System.Globalization.NumberStyles]::Float, $invariant, [ref]$value)) { return $value }
    return -1.0
}
function XForSeconds($seconds) {
    $frame = [int64]($seconds * 48000)
    return [int][Gui]::SendMessageW($GLOBAL:view, 0x8065, [IntPtr]$frame, [IntPtr]0)
}
function ClickWave($view, $x, $y = 100) {
    $lp = [IntPtr](($x -band 0xFFFF) -bor (($y -band 0xFFFF) -shl 16))
    [void][Gui]::SendMessageW($view, 0x0201, [IntPtr]1, $lp)
    [void][Gui]::SendMessageW($view, 0x0202, [IntPtr]0, $lp)
    Start-Sleep -Milliseconds 120
}
function DragWave($view, $x1, $x2, $y = 100) {
    $lp1 = [IntPtr](($x1 -band 0xFFFF) -bor (($y -band 0xFFFF) -shl 16))
    $lp2 = [IntPtr](($x2 -band 0xFFFF) -bor (($y -band 0xFFFF) -shl 16))
    [void][Gui]::SendMessageW($view, 0x0201, [IntPtr]1, $lp1)
    Start-Sleep -Milliseconds 60
    $steps = 6
    for ($i = 1; $i -le $steps; $i++) {
        $x = [int]($x1 + ($x2 - $x1) * $i / $steps)
        $lp = [IntPtr](($x -band 0xFFFF) -bor (($y -band 0xFFFF) -shl 16))
        [void][Gui]::SendMessageW($view, 0x0200, [IntPtr]1, $lp)
        Start-Sleep -Milliseconds 30
    }
    [void][Gui]::SendMessageW($view, 0x0202, [IntPtr]0, $lp2)
    Start-Sleep -Milliseconds 200
}
function Key($vk, $target = $null) {
    if ($null -eq $target) { $target = $script:hwnd }
    [void][Gui]::SendMessageW($target, 0x0100, [IntPtr]$vk, [IntPtr]0)
    [void][Gui]::SendMessageW($target, 0x0101, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 200
}
function Command($id) {
    [void][Gui]::PostMessageW($script:hwnd, 0x0111, [IntPtr]$id, [IntPtr]0)
    Start-Sleep -Milliseconds 250
}
function ClickControl($hwnd) {
    [void][Gui]::SendMessageW($hwnd, 0x00F5, [IntPtr]0, [IntPtr]0)
    Start-Sleep -Milliseconds 250
}

$proc = Start-Process -WindowStyle Normal -FilePath $studio -ArgumentList @('--session', "`"$sessionPath`"") -PassThru
$script:hwnd = [IntPtr]::Zero
$script:procId = $proc.Id
$script:dialogHandle = [IntPtr]::Zero
$script:hasModern = $false
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { throw "bc_studio e' uscito subito ($($proc.ExitCode))" }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $script:hwnd = $proc.MainWindowHandle; break }
}
if ($script:hwnd -eq [IntPtr]::Zero) { throw 'finestra GUI non comparsa' }
Start-Sleep -Milliseconds 700

$view = [Gui]::GetDlgItem($script:hwnd, 2000)
$panel = [Gui]::GetDlgItem($script:hwnd, 3201)
$rect = New-Object Gui+RECT
[void][Gui]::GetWindowRect($view, [ref]$rect)
$waveWidth = $rect.right - $rect.left
$totalSeconds = 6.4
$GLOBAL:view = $view
Check 'forma d onda e pannello presenti' ($view -ne [IntPtr]::Zero -and $panel -ne [IntPtr]::Zero)
Check 'due regioni caricate' ((PanelValue $panel 3105) -eq '-') 'pannello non deve avere selezione iniziale'

# --- clic sul centro della prima regione ---------------------------------
$captureBefore = Join-Path $work 'selezione_prima.png'
CaptureViewPng $view $captureBefore
ClickWave $view (XForSeconds 1.5)
$start1 = Seconds (PanelValue $panel 3105)
$end1 = Seconds (PanelValue $panel 3106)
Check 'clic sul respiro seleziona la regione' ($start1 -gt 0.9 -and $start1 -lt 1.1 -and $end1 -gt 1.9 -and $end1 -lt 2.1) "inizio=$start1 fine=$end1"
Check 'pannello mostra Incluso' (PanelHas $panel 'Incluso')
Check 'contatori mostrano 2 inclusi' (PanelHas $panel 'Inclusi 2')

# --- verifica pixel reale della selezione (rendering Win32) -------------
$captureAfter = Join-Path $work 'selezione_dopo.png'
Start-Sleep -Milliseconds 250
CaptureViewPng $view $captureAfter
$brightBefore = CountBrightPixels $captureBefore
$brightAfter = CountBrightPixels $captureAfter
Check 'la selezione aggiunge pixel di bordo luminosi visibili' `
    ($brightAfter -gt $brightBefore + 12) "pixel chiari prima=$brightBefore dopo=$brightAfter"


# --- trascinamento del centro: durata invariata, entrambi i confini ------
$centerX = XForSeconds (($start1 + $end1) / 2.0)
DragWave $view $centerX ($centerX + 30)
$start2 = Seconds (PanelValue $panel 3105)
$end2 = Seconds (PanelValue $panel 3106)
$duration1 = $end1 - $start1
$duration2 = $end2 - $start2
Check 'trascinando il centro si sposta la regione' ($start2 -gt $start1 + 0.15) "inizio $start1 -> $start2"
Check 'spostando il centro la durata resta uguale' ([math]::Abs($duration2 - $duration1) -lt 0.03) "durata $duration1 -> $duration2"

# --- Undo/Redo atomici del trascinamento -------------------------------
Command 1100  # Ctrl+Z / Annulla: un solo gesto, non centinaia di passi
ClickWave $view (XForSeconds (($start1 + $end1) / 2.0))
$startUndo = Seconds (PanelValue $panel 3105)
$endUndo = Seconds (PanelValue $panel 3106)
Check 'Undo riporta la regione alla posizione precedente' `
    ([math]::Abs($startUndo - $start1) -lt 0.04 -and [math]::Abs($endUndo - $end1) -lt 0.04) `
    "prima=$start1 dopo=$startUndo docStart=$([Gui]::SendMessageW($script:hwnd,0x812e,[IntPtr]0,[IntPtr]0)) docEnd=$([Gui]::SendMessageW($script:hwnd,0x812f,[IntPtr]0,[IntPtr]0)) selected=$([Gui]::SendMessageW($script:hwnd,0x8132,[IntPtr]0,[IntPtr]0)) undo=$([Gui]::SendMessageW($script:hwnd,0x8133,[IntPtr]0,[IntPtr]0))"
Command 1101  # Ctrl+Y / Ripristina
ClickWave $view (XForSeconds (($start2 + $end2) / 2.0))
$startRedo = Seconds (PanelValue $panel 3105)
Check 'Redo riporta la regione alla posizione modificata' `
    ([math]::Abs($startRedo - $start2) -lt 0.04) "atteso=$start2 trovato=$startRedo"
$start1 = $start2
$end1 = $end2
$start2 = $startRedo
$end2 = $startRedo + ($end1 - $start1)

# --- bordo sinistro ------------------------------------------------------
$leftX = XForSeconds $start2
DragWave $view $leftX ($leftX - 25)
$start3 = Seconds (PanelValue $panel 3105)
$end3 = Seconds (PanelValue $panel 3106)
Check 'il bordo sinistro modifica solo l inizio' ($start3 -lt $start2 - 0.10 -and [math]::Abs($end3 - $end2) -lt 0.03) "inizio $start2 -> $start3 fine $end2 -> $end3"

# --- bordo destro --------------------------------------------------------
$rightX = XForSeconds $end3
DragWave $view $rightX ($rightX + 25)
$start4 = Seconds (PanelValue $panel 3105)
$end4 = Seconds (PanelValue $panel 3106)
Check 'il bordo destro modifica solo la fine' ($end4 -gt $end3 + 0.10 -and [math]::Abs($start4 - $start3) -lt 0.03) "fine $end3 -> $end4 inizio $start3 -> $start4"

# --- esclusione e reinserimento -----------------------------------------
$centerX = XForSeconds (($start4 + $end4) / 2.0)
ClickWave $view $centerX
Key 0x52  # R esclude
Check 'R segna l evento come escluso' (PanelHas $panel 'Escluso')
Check 'i contatori passano a 1 incluso e 1 escluso' ((PanelHas $panel 'Inclusi 1') -and (PanelHas $panel 'Esclusi 1'))
Key 0x41  # A include
Check 'A reinserisce l evento' (PanelHas $panel 'Inclusi 2')

# --- Canc esclude, non elimina -----------------------------------------
ClickWave $view $centerX
Check 'prima di Canc la regione e selezionata' (PanelHas $panel 'Respiro selezionato')
Key 0x2E $view  # Canc: come l'utente, con il fuoco sulla forma d onda
Check 'Canc esclude l evento senza eliminarlo' (PanelHas $panel 'Escluso')
Check 'i contatori restano a 2 regioni' (PanelHas $panel 'Rilevati 2')
ClickControl ([Gui]::GetDlgItem($panel, 1313))  # Includi
Check 'dopo Canc l evento si puo reincludere' (PanelHas $panel 'Inclusi 2')

# --- pulsanti contestuali: Escludi / Includi ---------------------------
ClickWave $view $centerX
ClickControl ([Gui]::GetDlgItem($panel, 1314))  # Escludi
Check 'il pulsante Escludi funziona' (PanelHas $panel 'Escluso')
ClickControl ([Gui]::GetDlgItem($panel, 1313))  # Includi
Check 'il pulsante Includi funziona' (PanelHas $panel 'Inclusi 2')

# --- modalita' per singolo evento --------------------------------------
ClickWave $view $centerX
ClickControl ([Gui]::GetDlgItem($panel, 1316))  # Taglia
Check 'la modalita Taglia e applicata all evento' (PanelHas $panel 'Intervento: Taglia')
Command 1100  # Undo
ClickWave $view $centerX
Check 'Undo ripristina la modalita Attenua' (PanelHas $panel 'Intervento: Attenua')
Command 2322  # modalita' globale Dimezza
Check 'la modalita globale Dimezza raggiunge l evento selezionato' (PanelHas $panel 'Intervento: Dimezza')
Command 1100  # Undo globale
Start-Sleep -Milliseconds 100

# --- regione manuale: stessa parita' delle automatiche -------------------
DragWave $view (XForSeconds 4.8) (XForSeconds 5.4)
$selStart = [int64][Gui]::SendMessageW($script:hwnd, 0x8136, [IntPtr]0, [IntPtr]0)
$selEnd = [int64][Gui]::SendMessageW($script:hwnd, 0x8137, [IntPtr]0, [IntPtr]0)
Command 1308  # Aggiungi regione sulla selezione
$manualCount = [int][Gui]::SendMessageW($script:hwnd, 0x812D, [IntPtr]0, [IntPtr]0)  # WM_APP+301
Check 'la regione manuale viene aggiunta' ($manualCount -eq 3) "regioni=$manualCount"
$manualIndex = [int][Gui]::SendMessageW($script:hwnd, 0x8132, [IntPtr]0, [IntPtr]0)  # WM_APP+306
Check 'la regione manuale e selezionata' ($manualIndex -eq 2) "indice=$manualIndex"
ClickControl ([Gui]::GetDlgItem($panel, 1314))  # Escludi
$manualEnabled = [int][Gui]::SendMessageW($script:hwnd, 0x8130, [IntPtr]2, [IntPtr]0)  # WM_APP+304
Check 'la regione manuale si puo escludere' ($manualEnabled -eq 0)
Command 1100  # Undo dell'esclusione
$manualEnabledAfterUndo = [int][Gui]::SendMessageW($script:hwnd, 0x8130, [IntPtr]2, [IntPtr]0)
Check 'Undo ripristina l inclusione della regione manuale' ($manualEnabledAfterUndo -ne 0)

# --- anteprima usa il piano aggiornato ----------------------------------
Command 1208  # anteprima
$status = TextOf ([Gui]::GetDlgItem($script:hwnd, 2109))
Check 'l anteprima viene generata' ($status -like '*anteprima*')

# --- zoom verticale -----------------------------------------------------
$slider = [Gui]::GetDlgItem($panel, 3104)
[void][Gui]::SendMessageW($slider, 0x0405, [IntPtr]1, [IntPtr]2)  # TBM_SETPOS
[void][Gui]::SendMessageW($script:hwnd, 0x0114, [IntPtr]0, $slider)  # WM_HSCROLL
Start-Sleep -Milliseconds 200
$zoomValue = TextOf ([Gui]::GetDlgItem($panel, 3107))
Check 'zoom verticale mostra 4x' ($zoomValue -eq '4x') "valore=$zoomValue"

# --- zoom orizzontale senza perdere la selezione ------------------------
ClickControl ([Gui]::GetDlgItem($panel, 1400))  # Zoom +
$startAfterZoom = Seconds (PanelValue $panel 3105)
$endAfterZoom = Seconds (PanelValue $panel 3106)
Check 'zoom orizzontale mantiene la selezione' ((PanelHas $panel 'Respiro selezionato') -and $startAfterZoom -gt 0.0 -and $endAfterZoom -gt $startAfterZoom) "inizio=$startAfterZoom fine=$endAfterZoom"

# --- follow playhead ----------------------------------------------------
[void][Gui]::SendMessageW($script:hwnd, 0x0111, [IntPtr]2331, [IntPtr]0)
Start-Sleep -Milliseconds 400
$followState = [int][Gui]::SendMessageW($script:hwnd, 0x813E, [IntPtr]0, [IntPtr]0)
Check 'segui cursore si disattiva' ($followState -eq 0)
Check 'lo stato mostra segui cursore disattivato' ((TextOf ([Gui]::GetDlgItem($script:hwnd, 2109))) -like '*segui cursore disattivato*')
[void][Gui]::SendMessageW($script:hwnd, 0x0111, [IntPtr]2331, [IntPtr]0)
Start-Sleep -Milliseconds 400
$followState = [int][Gui]::SendMessageW($script:hwnd, 0x813E, [IntPtr]0, [IntPtr]0)
Check 'lo stato mostra segui cursore attivo' ((TextOf ([Gui]::GetDlgItem($script:hwnd, 2109))) -like '*segui cursore attivo*')

# --- eliminazione definitiva della regione manuale ----------------------
Command 1300  # Elimina definitivamente la regione selezionata
Start-Sleep -Milliseconds 150
$afterDelete = [int][Gui]::SendMessageW($script:hwnd, 0x812D, [IntPtr]0, [IntPtr]0)
Check 'la regione manuale si elimina definitivamente' ($afterDelete -eq 2) "regioni=$afterDelete"

# --- nuova analisi: le correzioni manuali e le esclusioni sopravvivono ----
Command 1402  # adatta alla finestra
Start-Sleep -Milliseconds 250
DragWave $view (XForSeconds 4.8) (XForSeconds 5.4)
Command 1308  # aggiungi regione manuale
Start-Sleep -Milliseconds 200
$manualIndex2 = [int][Gui]::SendMessageW($script:hwnd, 0x8132, [IntPtr]0, [IntPtr]0)
ClickControl ([Gui]::GetDlgItem($panel, 1314))  # Escludi manuale
$manualStart2 = [int64][Gui]::SendMessageW($script:hwnd, 0x812E, [IntPtr]$manualIndex2, [IntPtr]0)
$countBeforeAnalyze = [int][Gui]::SendMessageW($script:hwnd, 0x812D, [IntPtr]0, [IntPtr]0)
Command 1200  # analizza di nuovo
Start-Sleep -Seconds 2
$countAfterAnalyze = [int][Gui]::SendMessageW($script:hwnd, 0x812D, [IntPtr]0, [IntPtr]0)
$manualStillThere = $false
for ($i = 0; $i -lt $countAfterAnalyze; $i++) {
    $rs = [int64][Gui]::SendMessageW($script:hwnd, 0x812E, [IntPtr]$i, [IntPtr]0)
    $re = [int64][Gui]::SendMessageW($script:hwnd, 0x812F, [IntPtr]$i, [IntPtr]0)
    $en = [int][Gui]::SendMessageW($script:hwnd, 0x8130, [IntPtr]$i, [IntPtr]0)
    if ([math]::Abs($rs - $manualStart2) -lt 200 -and $en -eq 0) { $manualStillThere = $true }
}
Check 'la nuova analisi conserva la regione manuale esclusa' $manualStillThere "regioni=$countAfterAnalyze manuale=$manualStart2"
Command 1100  # Undo dell'intera analisi
Start-Sleep -Milliseconds 300
$countAfterUndo = [int][Gui]::SendMessageW($script:hwnd, 0x812D, [IntPtr]0, [IntPtr]0)
Check 'Undo annulla la nuova analisi in un solo passo' ($countAfterUndo -eq $countBeforeAnalyze) "prima=$countBeforeAnalyze dopo=$countAfterUndo"

# --- selettore cartelle moderno ----------------------------------------
Command 2200  # Progetto -> Apri cartella
Start-Sleep -Seconds 1
$dialog = [IntPtr]::Zero
for ($attempt = 0; $attempt -lt 20 -and $dialog -eq [IntPtr]::Zero; $attempt++) {
    $callback = [Gui+EnumProc]{
        param($hwnd, $unused)
        $windowPid = 0
        [void][Gui]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
        if ($windowPid -eq $script:procId -and [Gui]::IsWindowVisible($hwnd)) {
            $cls = New-Object System.Text.StringBuilder 128
            [void][Gui]::GetClassNameW($hwnd, $cls, 128)
            if ($cls.ToString() -eq '#32770') {
                $script:dialogHandle = $hwnd
            }
        }
        return $true
    }
    [void][Gui]::EnumWindows($callback, [IntPtr]::Zero)
    if ($script:dialogHandle -ne [IntPtr]::Zero) { $dialog = $script:dialogHandle }
    if ($dialog -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 250 }
}
Check 'il selettore cartelle si apre' ($dialog -ne [IntPtr]::Zero)
if ($dialog -ne [IntPtr]::Zero) {
    $hasModern = $false
    $childCallback = [Gui+EnumProc]{
        param($hwnd, $unused)
        $cls = New-Object System.Text.StringBuilder 128
        [void][Gui]::GetClassNameW($hwnd, $cls, 128)
        if ($cls.ToString() -like '*DirectUI*' -or $cls.ToString() -eq 'DUIViewWndClassName') {
            $script:hasModern = $true
        }
        return $true
    }
    [void][Gui]::EnumChildWindows($dialog, $childCallback, [IntPtr]::Zero)
    $hasModern = $script:hasModern
    Check 'usa il dialogo moderno IFileOpenDialog' $hasModern
    [void][Gui]::PostMessageW($dialog, 0x0010, [IntPtr]0, [IntPtr]0)
    Start-Sleep -Milliseconds 500
}

# --- chiusura e integrita' dell'originale -------------------------------
[void][Gui]::PostMessageW($script:hwnd, 0x0010, [IntPtr]0, [IntPtr]0)
Start-Sleep -Milliseconds 500
$proc.Refresh()
Check 'la finestra si chiude senza crash' $proc.HasExited
$hashAfter = FileHash $wav
Check 'il WAV originale non e stato modificato' ($hashBefore -eq $hashAfter)

Write-Host ''
Write-Host "GUI INTERACTION TEST: $($script:pass) OK, $($script:fail) falliti"
if ($script:fail -gt 0) { exit 1 }
Remove-Item -Recurse -Force $work
exit 0
