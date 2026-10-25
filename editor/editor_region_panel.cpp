// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  editor_region_panel.cpp — pannello contestuale dell'evento e legenda
//
//  Il pannello mostra sempre l'evento scelto: stato (incluso/escluso),
//  confini, durata, modalita' e fade. I confini si possono correggere con il
//  mouse oppure numericamente da un dialogo, senza cercare comandi nei menu.
// ============================================================================
#include "editor_window.h"

#ifdef _WIN32

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

#include <commctrl.h>

#include "editor_commands.h"
#include "studio_engine.h"

using namespace bc;

namespace {

constexpr int kRegionPanelWidth = 300;
constexpr int kBoundsDialogOk = 1;
constexpr int kBoundsDialogCancel = 2;
constexpr int kBoundsDialogStart = 101;
constexpr int kBoundsDialogEnd = 102;

std::wstring fmtSeconds(double value) {
    wchar_t buffer[64] = {};
    swprintf(buffer, 64, L"%.3f s", value);
    return buffer;
}

const wchar_t* modeName(int mode) {
    switch (mode) {
        case 1: return L"Taglia";
        case 2: return L"Dimezza";
        default: return L"Attenua";
    }
}

struct BoundsDialogState {
    HWND window = nullptr;
    HWND startEdit = nullptr;
    HWND endEdit = nullptr;
    HWND error = nullptr;
    HFONT font = nullptr;
    bool accepted = false;
    double start = 0.0;
    double end = 0.0;
};

LRESULT CALLBACK boundsDialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<BoundsDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<BoundsDialogState*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto label = [&](const wchar_t* text, int x, int y, int w, int h) {
                HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                               x, y, w, h, window, nullptr, nullptr, nullptr);
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                return control;
            };
            label(L"Inizio evento (secondi)", 16, 18, 330, 20);
            state->startEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0.000",
                                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                               16, 40, 120, 26, window,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoundsDialogStart)),
                                               nullptr, nullptr);
            SendMessageW(state->startEdit, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);
            label(L"Fine evento (secondi)", 16, 76, 330, 20);
            state->endEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0.000",
                                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                             16, 98, 120, 26, window,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoundsDialogEnd)),
                                             nullptr, nullptr);
            SendMessageW(state->endEdit, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);
            state->error = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                           16, 132, 340, 38, window, nullptr, nullptr, nullptr);
            SendMessageW(state->error, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);
            HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
                                      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                      180, 176, 80, 28, window,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoundsDialogOk)),
                                      nullptr, nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"Annulla",
                                          WS_CHILD | WS_VISIBLE,
                                          268, 176, 80, 28, window,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoundsDialogCancel)),
                                          nullptr, nullptr);
            SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SetFocus(state->startEdit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kBoundsDialogOk) {
                wchar_t startText[64] = {};
                wchar_t endText[64] = {};
                GetWindowTextW(state->startEdit, startText, 64);
                GetWindowTextW(state->endEdit, endText, 64);
                wchar_t* end = nullptr;
                const double start = std::wcstod(startText, &end);
                if (end == startText || *end != L'\0' || !std::isfinite(start) || start < 0.0) {
                    SetWindowTextW(state->error, L"Inizio non valido: usare un numero >= 0.");
                    return 0;
                }
                const double finish = std::wcstod(endText, &end);
                if (end == endText || *end != L'\0' || !std::isfinite(finish) ||
                    finish <= start) {
                    SetWindowTextW(state->error, L"Fine non valida: deve essere maggiore dell'inizio.");
                    return 0;
                }
                state->start = start;
                state->end = finish;
                state->accepted = true;
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wParam) == kBoundsDialogCancel) {
                DestroyWindow(window);
                return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK regionPanelSubclass(HWND window, UINT message, WPARAM wParam,
                                      LPARAM lParam, UINT_PTR, DWORD_PTR) {
    // I controlli del pannello hanno il pannello come genitore. I loro
    // WM_COMMAND/WM_HSCROLL arrivano qui: vanno inoltrati alla finestra
    // principale, che e' l'unica che conosce i comandi dell'editor.
    if (message == WM_COMMAND || message == WM_HSCROLL) {
        SendMessageW(GetParent(window), message, wParam, lParam);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

bool showBoundsDialog(HWND parent, double start, double end,
                      double* outStart, double* outEnd) {
    static constexpr wchar_t kClass[] = L"BreathClickBoundsDialog";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = boundsDialogProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    BoundsDialogState state;
    RECT owner = {};
    if (parent) GetWindowRect(parent, &owner);
    const int x = parent ? owner.left + 100 : CW_USEDEFAULT;
    const int y = parent ? owner.top + 100 : CW_USEDEFAULT;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Confini evento",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, 390, 245, parent, nullptr,
                                  GetModuleHandleW(nullptr), &state);
    if (!window) return false;
    wchar_t startText[64] = {};
    wchar_t endText[64] = {};
    swprintf(startText, 64, L"%.3f", start);
    swprintf(endText, 64, L"%.3f", end);
    SetWindowTextW(state.startEdit, startText);
    SetWindowTextW(state.endEdit, endText);
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message = {};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (parent) EnableWindow(parent, TRUE);
    if (state.accepted) {
        if (outStart) *outStart = state.start;
        if (outEnd) *outEnd = state.end;
    }
    return state.accepted;
}

} // namespace

void EditorWindow::createRegionPanel() {
    regionPanel_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | WS_BORDER,
        0, kToolbarHeight, kRegionPanelWidth, 100, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(3201)),
        instance_, nullptr);
    if (!regionPanel_) return;
    SetWindowSubclass(regionPanel_, regionPanelSubclass, 1, 0);

    auto makeStatic = [&](const wchar_t* text, int x, int y, int w, int h) {
        if (y >= 70) y += 36;
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       x, y, w, h, regionPanel_, nullptr, instance_, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return control;
    };
    auto makeButton = [&](int id, const wchar_t* text, int x, int y, int w, int h) {
        if (y >= 70) y += 36;
        HWND control = CreateWindowExW(0, L"BUTTON", text,
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       x, y, w, h, regionPanel_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       instance_, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return control;
    };

    regionTitle_ = makeStatic(L"Respiro selezionato", 8, 4, 280, 20);
    regionCounters_ = makeStatic(L"Rilevati 0 | Inclusi 0 | Esclusi 0 | Manuali 0",
                                 8, 24, 280, 50);
    regionState_ = makeStatic(L"Nessun evento selezionato", 8, 58, 280, 18);
    MoveWindow(regionState_, 8, 80, 280, 18, FALSE);
    makeStatic(L"Inizio", 8, 70, 62, 18);
    regionStartValue_ = makeStatic(L"-", 74, 70, 96, 18);
    SetWindowLongPtrW(regionStartValue_, GWLP_ID, 3105);
    makeStatic(L"Fine", 176, 70, 44, 18);
    regionEndValue_ = makeStatic(L"-", 222, 70, 70, 18);
    SetWindowLongPtrW(regionEndValue_, GWLP_ID, 3106);
    regionApply_ = makeButton(EditorCommand::kRegionEditBounds, L"Modifica confini...",
                              8, 94, 150, 26);
    regionDuration_ = makeStatic(L"Durata: -", 166, 98, 126, 18);
    regionMode_ = makeStatic(L"Intervento: -", 8, 128, 284, 18);
    regionFade_ = makeStatic(L"Fade: -", 8, 150, 284, 18);
    regionApprove_ = makeButton(EditorCommand::kRegionConfirm, L"Includi", 8, 176, 135, 28);
    regionReject_ = makeButton(EditorCommand::kRegionReject, L"Escludi", 151, 176, 135, 28);

    makeStatic(L"Modalita' dell'evento", 8, 212, 284, 18);
    regionModeButtons_[0] = makeButton(EditorCommand::kRegionModeAttenuate, L"Attenua", 8, 232, 88, 26);
    regionModeButtons_[1] = makeButton(EditorCommand::kRegionModeCut, L"Taglia", 102, 232, 88, 26);
    regionModeButtons_[2] = makeButton(EditorCommand::kRegionModeHalve, L"Dimezza", 196, 232, 88, 26);

    makeStatic(L"Ampiezza (solo vista)", 8, 266, 284, 18);
    verticalZoomSlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                          WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                          8, 322, 220, 22, regionPanel_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(3104)),
                                          instance_, nullptr);
    SendMessageW(verticalZoomSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 3));
    SendMessageW(verticalZoomSlider_, TBM_SETPOS, TRUE, 0);
    verticalZoomValue_ = makeStatic(L"1x", 234, 288, 48, 18);
    SetWindowLongPtrW(verticalZoomValue_, GWLP_ID, 3107);
    makeButton(EditorCommand::kViewZoomOut, L"Zoom -", 8, 316, 86, 26);
    makeButton(EditorCommand::kViewZoomFit, L"Adatta", 102, 316, 86, 26);
    makeButton(EditorCommand::kViewZoomIn, L"Zoom +", 196, 316, 86, 26);
    toolRegions_ = makeButton(EditorCommand::kToolRegions, L"REGIONI", 8, 348, 135, 26);
    toolTime_ = makeButton(EditorCommand::kToolTimeSelection, L"SELEZIONE TEMPO", 151, 348, 135, 26);
    regionFilter_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        8, 416, 278, 220, regionPanel_, reinterpret_cast<HMENU>(EditorCommand::kRegionFilter), instance_, nullptr);
    for (const wchar_t* text : {L"Tutti gli eventi", L"Respiri", L"Click", L"Manuali", L"Automatici", L"Esclusi", L"Incerti / possibile parlato", L"Click digitali", L"Saliva / labbra", L"Falsi positivi verificati"})
        SendMessageW(regionFilter_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(regionFilter_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(regionFilter_, CB_SETCURSEL, 0, 0);
    makeButton(EditorCommand::kRegionsInTimeSelection, L"Eventi nell'intervallo", 8, 412, 135, 26);
    makeButton(EditorCommand::kClicksDisable, L"Escludi tutti i click", 151, 412, 135, 26);
    makeButton(EditorCommand::kManualAttenuate, L"Attenua selezione...", 8, 444, 135, 26);
    makeButton(EditorCommand::kManualSilence, L"Silenzia selezione", 151, 444, 135, 26);
    makeButton(EditorCommand::kManualCut, L"Taglia selezione", 8, 476, 135, 26);
    makeButton(EditorCommand::kManualHalve, L"Dimezza durata", 151, 476, 135, 26);
    makeButton(EditorCommand::kManualRestore, L"Ripristina intervallo", 8, 508, 135, 26);
    makeButton(EditorCommand::kTimeSelectionBounds, L"Confini selezione...", 151, 508, 135, 26);
    learningSummary_ = makeStatic(L"", 8, 540, 278, 58);
    SetWindowLongPtrW(learningSummary_, GWLP_ID, 3110);

    legend_ = CreateWindowExW(0, L"STATIC", L"",
                              WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                              0, 0, 100, kLegendHeight, window_, nullptr, instance_, nullptr);
    SendMessageW(legend_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    updateLegend();
}

void EditorWindow::layoutRegionPanel() {
    if (!regionPanel_) return;
    RECT client = {};
    GetClientRect(window_, &client);
    const int width = std::min(kRegionPanelWidth, std::max(240, static_cast<int>(client.right) / 4));
    const int height = std::max(80, static_cast<int>(client.bottom) - kToolbarHeight -
                                     kStatusHeight - kLegendHeight);
    MoveWindow(regionPanel_, client.right - width, kToolbarHeight, width, height, TRUE);
    if (legend_) {
        MoveWindow(legend_, 8, client.bottom - kStatusHeight - kLegendHeight,
                   std::max(100, static_cast<int>(client.right) - 16), kLegendHeight, TRUE);
    }
}

void EditorWindow::updateLegend() {
    if (!legend_) return;
    SetWindowTextW(legend_,
        L"Onda originale | Giallo: respiro | Rosso: click | Ciano: manuale | Grigio: escluso | Blu/viola: selezionato | "
        L"Ctrl+clic multipla, Shift+clic intervallo | Esc azzera selezione | Ctrl+Z/Y annulla/ripristina");
}

void EditorWindow::refreshRegionPanel(bool force) {
    (void)force;
    if (!regionPanel_ || regionPanelUpdating_) return;
    regionPanelUpdating_ = true;

    int included = 0;
    int excluded = 0;
    int manual = 0;
    int clicks = 0, detected = 0;
    if (document_.isLoaded()) {
        for (const EditRegion& region : document_.regions()) {
            if (region.enabled && region.approved) ++included;
            else ++excluded;
            if (!region.fromDetection) ++manual;
            else ++detected;
            if (region.kind == DetectionKind::Click) ++clicks;
        }
    }

    wchar_t counters[180] = {};
    swprintf(counters, 180, L"Rilevati %d | Inclusi %d | Esclusi %d\nManuali %d | Click %d\n",
             detected, included, excluded, manual, clicks);
    std::wstring countText = counters;
    countText += engineApplied_ < 0 ? L"Motore: anteprima da aggiornare" :
        L"Applicati " + std::to_wstring(engineApplied_) + L" | Rifiutati " + std::to_wstring(engineRejected_);
    SetWindowTextW(regionCounters_, countText.c_str());
    SendMessageW(toolRegions_, BM_SETSTATE, !view_.timeSelectionTool(), 0);
    SendMessageW(toolTime_, BM_SETSTATE, view_.timeSelectionTool(), 0);
    updateLearningSummary();

    const int index = document_.isLoaded() ? view_.selectedRegion() : -1;
    const bool valid = index >= 0 && index < static_cast<int>(document_.regions().size());
    if (!valid) {
        SetWindowTextW(regionTitle_, L"Nessun evento selezionato");
        SetWindowTextW(regionState_, L"Clicca una regione sulla forma d'onda");
        SetWindowTextW(regionStartValue_, L"-");
        SetWindowTextW(regionEndValue_, L"-");
        SetWindowTextW(regionDuration_, L"Durata: -");
        SetWindowTextW(regionMode_, L"Intervento: -");
        SetWindowTextW(regionFade_, L"Fade: -");
        EnableWindow(regionApply_, FALSE);
        EnableWindow(regionApprove_, FALSE);
        EnableWindow(regionReject_, FALSE);
        for (HWND button : regionModeButtons_) if (button) EnableWindow(button, FALSE);
        regionPanelUpdating_ = false;
        return;
    }

    const EditRegion& region = document_.regions()[static_cast<size_t>(index)];
    const bool isIncluded = region.enabled && region.approved;
    const bool isRejected = !isIncluded && region.decisionSource == "user";
    SetWindowTextW(regionTitle_, region.manualOperation ? L"Intervento manuale selezionato" : region.kind == DetectionKind::Breath
                                     ? L"Respiro selezionato"
                                     : L"Click selezionato");
    if(region.kind==DetectionKind::Click)
        SetWindowTextW(regionTitle_,region.classification=="digital_click"?L"Click digitale":
            region.classification=="saliva_click"?L"Click di saliva breve":
            region.classification=="mouth_click"?L"Schiocco saliva/labbra":
            region.classification=="speech_overlap"?L"Click sovrapposto alla voce - rivedere":L"Click selezionato");
    SetWindowTextW(regionState_, (region.learningLabel == "false_positive" || region.learningLabel == "click_false_positive" || region.learningLabel=="vocal_transient") ? L"Falso positivo verificato (negativo)" : isIncluded
        ? (region.trainingEligible ? (region.kind==DetectionKind::Click?L"Incluso - label click salvata":L"Incluso - confermato training") : L"Incluso")
        : (isRejected ? L"Escluso (resta nel progetto)" : L"Escluso / non incluso"));

    const double sampleRate = std::max(1, document_.sampleRate());
    SetWindowTextW(regionStartValue_,
                   fmtSeconds(static_cast<double>(region.startSample) / sampleRate).c_str());
    SetWindowTextW(regionEndValue_,
                   fmtSeconds(static_cast<double>(region.endSample) / sampleRate).c_str());
    wchar_t duration[64] = {};
    swprintf(duration, 64, L"Durata: %.1f ms",
             static_cast<double>(region.endSample - region.startSample) /
                 sampleRate * 1000.0);
    SetWindowTextW(regionDuration_, duration);

    wchar_t mode[160] = {};
    if (static_cast<int>(region.editMode) == 0) {
        swprintf(mode, 160, L"Intervento: Attenua %.1f dB", static_cast<double>(region.reductionDb));
    } else if (static_cast<int>(region.editMode) == 2) {
        swprintf(mode, 160, L"Intervento: Dimezza %.0f%%", region.keepFraction * 100.0);
    } else {
        swprintf(mode, 160, L"Intervento: %s", modeName(region.editMode));
    }
    SetWindowTextW(regionMode_, mode);
    wchar_t fade[96] = {};
    swprintf(fade, 96, L"Fade: %.1f ms   |   Margine: %.1f ms",
             region.fadeMs, region.safetyMarginMs);
    SetWindowTextW(regionFade_, fade);
    if (region.fromDetection && !region.trainingEligible && region.classification == "dsp")
        SetWindowTextW(regionFade_, L"DSP solo: verificare possibile parlato");
    if (region.editMode == 3) SetWindowTextW(regionMode_, L"Intervento: Silenzio (durata invariata)");

    EnableWindow(regionApply_, TRUE);
    EnableWindow(regionApprove_, !isIncluded || view_.selectedRegions().size() > 1);
    EnableWindow(regionReject_, isIncluded || view_.selectedRegions().size() > 1);
    for (HWND button : regionModeButtons_) if (button) EnableWindow(button, TRUE);
    regionPanelUpdating_ = false;
}

void EditorWindow::commandTimeBounds() {
    if (!view_.hasSelection()) return;
    const double sr = std::max(1, document_.sampleRate());
    double start = view_.selectionStart() / sr, end = view_.selectionEnd() / sr;
    if (showBoundsDialog(window_, start, end, &start, &end))
        view_.setSelection(static_cast<int64_t>(std::llround(start * sr)), static_cast<int64_t>(std::llround(end * sr)));
}

void EditorWindow::updateLearningSummary() {
    if (!learningSummary_) return;
    int positive = 0, negative = 0, editorial = 0, unverified = 0, files = 0, clickPositive=0, clickNegative=0;
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        const AudioDocument* doc = trackDocument(i);
        if (!doc || !doc->isLoaded()) continue;
        bool verified = false;
        for (const auto& r : doc->regions()) {
            if(r.kind==DetectionKind::Click && r.trainingEligible && r.decisionSource=="user"){
                if(r.learningLabel=="click_confirmed" || r.learningLabel=="missed_click" || r.learningLabel=="mouth_click_confirmed")++clickPositive;
                if(r.learningLabel=="click_false_positive" || r.learningLabel=="vocal_transient")++clickNegative;
            }
            if (r.kind != DetectionKind::Breath || r.manualOperation) continue;
            const bool human = r.trainingEligible && r.decisionSource == "user";
            if (human && (r.learningLabel == "breath_confirmed" || r.learningLabel == "missed_breath")) { ++positive; verified = true; }
            else if (human && r.learningLabel == "false_positive") { ++negative; verified = true; }
            else if (r.learningLabel == "editorial_exclusion" || (!r.enabled && !human)) ++editorial;
            else ++unverified;
        }
        if (verified) ++files;
    }
    wchar_t text[256] = {};
    swprintf(text, 256, L"Training: +%d / -%d | Editoriali %d | Non verificati %d\n"
        L"File respiri: %d. %s\nClick (label separate): +%d / -%d", positive, negative, editorial, unverified, files,
        (files < 2 || positive < 6 || negative < 6) ? L"Esempi insufficienti" : L"Da validare su file indipendenti",clickPositive,clickNegative);
    SetWindowTextW(learningSummary_, text);
}

void EditorWindow::commandRegionEditBounds() {
    const int index = view_.selectedRegion();
    if (index < 0 || index >= static_cast<int>(document_.regions().size())) {
        setStatusMessage("selezionare un evento prima di modificarne i confini");
        return;
    }
    const EditRegion& region = document_.regions()[static_cast<size_t>(index)];
    const double sampleRate = std::max(1, document_.sampleRate());
    double start = static_cast<double>(region.startSample) / sampleRate;
    double end = static_cast<double>(region.endSample) / sampleRate;
    if (!showBoundsDialog(window_, start, end, &start, &end)) return;

    const int64_t frames = document_.frameCount();
    const int64_t startSample = std::max<int64_t>(0, std::min<int64_t>(
        frames - 1, static_cast<int64_t>(std::llround(start * sampleRate))));
    const int64_t endSample = std::max<int64_t>(startSample + 1, std::min<int64_t>(
        frames, static_cast<int64_t>(std::llround(end * sampleRate))));
    EditRegion updated = region;
    updated.startSample = startSample;
    updated.endSample = endSample;
    if (!document_.updateRegion(index, updated)) return;
    hasPreview_ = false;
    view_.refreshDocument(false);
    markDocumentChanged(false);
    refreshRegionPanel(true);
    setStatusMessage("confini aggiornati");
}

void EditorWindow::commandVerticalZoom(int step) {
    static constexpr double levels[] = {1.0, 2.0, 4.0, 8.0};
    int index = 0;
    const double current = view_.verticalZoom();
    for (int i = 0; i < 4; ++i) if (std::abs(levels[i] - current) < 0.01) index = i;
    index = std::max(0, std::min(3, index + step));
    view_.setVerticalZoom(levels[index]);
    if (verticalZoomSlider_) SendMessageW(verticalZoomSlider_, TBM_SETPOS, TRUE, index);
    if (verticalZoomValue_) {
        wchar_t label[16] = {};
        swprintf(label, 16, L"%.0fx", levels[index]);
        SetWindowTextW(verticalZoomValue_, label);
    }
    setStatusMessage(std::string("ampiezza vista: ") +
                     std::to_string(static_cast<int>(levels[index])) + "x");
}

void EditorWindow::commandToggleFollowPlayhead() {
    followPlayhead_ = !followPlayhead_;
    setStatusMessage(followPlayhead_ ? "segui cursore attivo" : "segui cursore disattivato");
    updateCommandState();
    updateStatusBar();
}

void EditorWindow::commandGoToPlayhead() {
    if (!document_.isLoaded()) return;
    if (player_.isPlaying()) view_.setPlayhead(playerPositionInDocument());
    followAutoScroll_ = true;
    view_.ensureFrameVisible(view_.playhead());
    followAutoScroll_ = false;
    setStatusMessage("vista riportata sulla posizione di riproduzione");
}

void EditorWindow::commandGoToSelectedRegion() {
    const int index = view_.selectedRegion();
    if (index < 0 || index >= static_cast<int>(document_.regions().size())) {
        setStatusMessage("nessun respiro selezionato");
        return;
    }
    const EditRegion& region = document_.regions()[static_cast<size_t>(index)];
    const int64_t center = (region.startSample + region.endSample) / 2;
    view_.ensureFrameVisible(center);
    view_.setPlayhead(region.startSample);
    updateStatusBar();
    setStatusMessage("vai al respiro selezionato");
}

void EditorWindow::commandVerticalZoomReset() {
    view_.setVerticalZoom(1.0);
    if (verticalZoomSlider_) SendMessageW(verticalZoomSlider_, TBM_SETPOS, TRUE, 0);
    if (verticalZoomValue_) SetWindowTextW(verticalZoomValue_, L"1x");
    setStatusMessage("ampiezza vista ripristinata a 1x");
}

void EditorWindow::createTooltips() {
    if (tooltip_) return;
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               window_, nullptr, instance_, nullptr);
    if (!tooltip_) return;

    auto add = [&](HWND control, const wchar_t* text) {
        if (!control) return;
        TOOLINFOW info = {};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = window_;
        info.uId = reinterpret_cast<UINT_PTR>(control);
        info.lpszText = const_cast<wchar_t*>(text);
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    };

    add(openButton_, L"Apri un file WAV");
    add(saveButton_, L"Salva il WAV corretto");
    add(exportButton_, L"Esporta WAV, report e piano");
    add(analyzeButton_, L"Analizza il file corrente");
    add(settingsButton_, L"Impostazioni di rilevamento");
    add(playButton_, L"Riproduci dall'inizio o dalla selezione");
    add(stopButton_, L"Ferma la riproduzione");
    add(regionApprove_, L"Includi l'evento nel piano");
    add(regionReject_, L"Escludi l'evento senza cancellarlo");
    add(regionApply_, L"Correggi inizio e fine numericamente");
    add(verticalZoomSlider_, L"Ampiezza della forma d'onda: 1x, 2x, 4x, 8x. Non cambia l'audio.");
}

#endif
