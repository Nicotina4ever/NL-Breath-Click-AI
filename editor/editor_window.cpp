// SPDX-License-Identifier: MPL-2.0
#include "version.h"
#include "editor_window.h"

#ifdef _WIN32

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include "core/detect/offline_analyzer.h"
#include "core/edit/wav_io.h"
#include "editor_commands.h"
#include "detection_preview.h"
#include "detection_preview_worker.h"
#include "detector_settings_store.h"
#include "studio_batch.h"
#include "studio_dialogs.h"
#include "studio_engine.h"
#include "studio_session.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")

using namespace bc;

namespace {

constexpr wchar_t kWindowClass[] = L"BreathClickEditorWindow";
constexpr int kIdWaveform = 2000;

// --- barra dei comandi -----------------------------------------------------
enum ToolbarId : int {
    kIdOpen = 2100,
    kIdSave,
    kIdExport,
    kIdAnalyze,
    kIdSettings,
    kIdPlay,
    kIdStop,
    kIdGain,
    kIdGainValue,
    kIdStatus
};

// --- finestra delle impostazioni -------------------------------------------
enum SettingsId : int {
    kIdSetFirst = 3000,
    kIdSetBreathThreshold = 3000,
    kIdSetClickThreshold,
    kIdSetBreathSensitivity,
    kIdSetClickSensitivity,
    kIdSetMinBreath,
    kIdSetMinClick,
    kIdSetPauseGate,
    kIdSetDevice,
    kIdSetOk,
    kIdSetCancel,
    kIdSetApply,
    kIdSetDefaults,
    kIdSetSavePreset,
    kIdSetLoadPreset,
    kIdSetMerge,
    kIdSetPresetConservative,
    kIdSetPresetBalanced,
    kIdSetPresetHigh,
    kIdPreviewCanvas,
    kIdPreviewStatus,
    kIdPreviewPlay,
    kIdPreviewOriginal
};

constexpr int kSliderResolution = 1000;

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring result(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), needed);
    return result;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string result(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), needed, nullptr, nullptr);
    return result;
}

std::string formatSeconds(double seconds) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.3f s", seconds);
    return buffer;
}

std::string formatTimecode(double seconds) {
    char buffer[64] = {};
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    std::snprintf(buffer, sizeof(buffer), "%d:%06.3f", minutes, rest);
    return buffer;
}

// Cartella dei profili, accanto all'eseguibile.
//
// Accanto e non nel progetto: i profili sono dati dell'utente, creati man
// mano o addestrati, e devono seguire il programma installato, non la
// cartella dei sorgenti.
std::string resolveModelsRoot() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};

    std::wstring full(path, length);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};

    const std::wstring cartella = full.substr(0, slash) + L"\\models";
    const int needed = WideCharToMultiByte(CP_UTF8, 0, cartella.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string risultato(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, cartella.c_str(), -1, risultato.data(),
                        needed, nullptr, nullptr);
    return risultato;
}

// ---------------------------------------------------------------------------
//  Finestra delle impostazioni di rilevamento
//
//  E' un dialogo modale semplice: la finestra principale resta bloccata finche'
//  non si chiude, cosi' i parametri non cambiano sotto un'analisi in corso.
// ---------------------------------------------------------------------------
struct DetectionPreviewContext {
    const AudioDocument* document = nullptr;
    DetectionPreviewWindow window;
    std::vector<Detection> before;
    std::shared_ptr<const std::vector<float>> auditionSamples;
    int sampleRate = 0;
    int channels = 0;
    EditSettings editSettings;
};

struct SettingsDialogState {
    DetectorSettings* settings = nullptr;
    const DetectionPreviewContext* preview = nullptr;
    bool committed = false;
    bool accepted = false;
    HWND breathThreshold = nullptr;
    HWND clickThreshold = nullptr;
    HWND breathSensitivity = nullptr;
    HWND clickSensitivity = nullptr;
    HWND minBreath = nullptr;
    HWND minClick = nullptr;
    HWND mergeGap = nullptr;
    HWND pauseGate = nullptr;
    HWND detectBreath = nullptr, detectClicks = nullptr;
    HWND device = nullptr;
    HWND breathValue = nullptr;
    HWND clickValue = nullptr;
    HWND breathSensValue = nullptr;
    HWND clickSensValue = nullptr;
    HWND minBreathValue = nullptr;
    HWND minClickValue = nullptr;
    HWND mergeValue = nullptr;
    HWND previewCanvas = nullptr;
    HWND previewStatus = nullptr;
    HWND previewPlay = nullptr;
    HWND previewOriginal = nullptr;
    AudioPlayer previewPlayer;
    HWND presetButtons[3] = {};
    HFONT font = nullptr;
    AnalysisResult previewResult;
    std::string previewError;
    bool previewDirty = true;
    std::unique_ptr<DetectionPreviewWorker> previewWorker;
    bool previewBusy = false;

};
SettingsDialogState* g_settingsDialog = nullptr;

constexpr UINT kPreviewTimer = 41;
constexpr UINT kPreviewPollTimer = 42;

void setControlFont(HWND control, HFONT font) {
    if (control && font) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

DetectorSettings readControls(SettingsDialogState& state) {
    DetectorSettings settings = state.settings ? *state.settings : DetectorSettings{};
    if (state.breathThreshold) {
        settings.detectBreath = SendMessageW(state.detectBreath,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.detectClicks = SendMessageW(state.detectClicks,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.breathThreshold =
            static_cast<float>(SendMessageW(state.breathThreshold, TBM_GETPOS, 0, 0)) /
            kSliderResolution;
        settings.clickThreshold =
            static_cast<float>(SendMessageW(state.clickThreshold, TBM_GETPOS, 0, 0)) /
            kSliderResolution;
        settings.breathSensitivity =
            static_cast<float>(SendMessageW(state.breathSensitivity, TBM_GETPOS, 0, 0)) /
            kSliderResolution;
        settings.clickSensitivity =
            static_cast<float>(SendMessageW(state.clickSensitivity, TBM_GETPOS, 0, 0)) /
            kSliderResolution;
        settings.minBreathMs = static_cast<int>(SendMessageW(state.minBreath, TBM_GETPOS, 0, 0));
        settings.minClickMs = static_cast<int>(SendMessageW(state.minClick, TBM_GETPOS, 0, 0));
        settings.mergeGapMs = static_cast<int>(SendMessageW(state.mergeGap, TBM_GETPOS, 0, 0));
        settings.requirePauseForBreath =
            SendMessageW(state.pauseGate, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    return settings;
}

void updateSettingsLabels(SettingsDialogState& state) {
    if (!state.settings) return;
    wchar_t buffer[64] = {};
    const int breathPercent = static_cast<int>(SendMessageW(state.breathThreshold, TBM_GETPOS, 0, 0) / 10.0);
    swprintf(buffer, 64, L"%d %%", breathPercent);
    SetWindowTextW(state.breathValue, buffer);
    const int clickPercent = static_cast<int>(SendMessageW(state.clickThreshold, TBM_GETPOS, 0, 0) / 10.0);
    swprintf(buffer, 64, L"%d %%", clickPercent);
    SetWindowTextW(state.clickValue, buffer);
    const int breathSens = static_cast<int>(SendMessageW(state.breathSensitivity, TBM_GETPOS, 0, 0) / 10.0);
    swprintf(buffer, 64, L"%d %%", breathSens);
    SetWindowTextW(state.breathSensValue, buffer);
    const int clickSens = static_cast<int>(SendMessageW(state.clickSensitivity, TBM_GETPOS, 0, 0) / 10.0);
    swprintf(buffer, 64, L"%d %%", clickSens);
    SetWindowTextW(state.clickSensValue, buffer);
    swprintf(buffer, 64, L"%d ms", static_cast<int>(SendMessageW(state.minBreath, TBM_GETPOS, 0, 0)));
    SetWindowTextW(state.minBreathValue, buffer);
    swprintf(buffer, 64, L"%d ms", static_cast<int>(SendMessageW(state.minClick, TBM_GETPOS, 0, 0)));
    SetWindowTextW(state.minClickValue, buffer);
    swprintf(buffer, 64, L"%d ms", static_cast<int>(SendMessageW(state.mergeGap, TBM_GETPOS, 0, 0)));
    SetWindowTextW(state.mergeValue, buffer);
}

void presentPreview(SettingsDialogState& state) {
    state.previewError = state.previewResult.error;
    if (state.previewCanvas) InvalidateRect(state.previewCanvas, nullptr, TRUE);
    if (state.previewStatus) {
        std::wstring text;
        if (!state.previewError.empty()) {
            text = L"Anteprima non disponibile: " + toWide(state.previewError);
        } else {
            const DetectionPreviewDelta delta = compareDetectionProposals(
                state.preview ? state.preview->before : std::vector<Detection>{},
                state.previewResult.detections);
            wchar_t buffer[240] = {};
            swprintf(buffer, 240,
                     L"Prima: %d   Dopo: %d   mantenuti: %d   aggiunti: %d   rimossi: %d",
                     delta.beforeCount(), delta.afterCount(), delta.kept,
                     delta.added, delta.removed);
            text = buffer;
        }
        SetWindowTextW(state.previewStatus, text.c_str());
    }
}

void invalidatePreview(SettingsDialogState& state) {
    if (state.previewWorker) state.previewWorker->cancel();
    state.previewPlayer.stop();
    if (state.previewPlay) SetWindowTextW(state.previewPlay, L"Ascolta Dopo");
    state.previewDirty = true;
    state.previewBusy = false;
    state.previewResult = AnalysisResult{};
    if (state.previewStatus) SetWindowTextW(state.previewStatus, L"Anteprima in preparazione...");
    if (state.previewCanvas) InvalidateRect(state.previewCanvas, nullptr, TRUE);
    if (state.previewCanvas) SetTimer(GetParent(state.previewCanvas), kPreviewTimer, 220, nullptr);
}

void updatePreview(SettingsDialogState& state) {
    state.previewDirty = false;
    if (!state.previewWorker) {
        state.previewResult.error = "Apri un file audio per vedere l'anteprima.";
        presentPreview(state);
        return;
    }
    state.previewBusy = true;
    state.previewWorker->submit(readControls(state));
    SetWindowTextW(state.previewStatus, L"Analisi locale in corso...");
}

void setSlidersFromSettings(SettingsDialogState& state, const DetectorSettings& settings) {
    SendMessageW(state.breathThreshold, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(settings.breathThreshold * kSliderResolution));
    SendMessageW(state.clickThreshold, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(settings.clickThreshold * kSliderResolution));
    SendMessageW(state.breathSensitivity, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(settings.breathSensitivity * kSliderResolution));
    SendMessageW(state.clickSensitivity, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(settings.clickSensitivity * kSliderResolution));
    SendMessageW(state.minBreath, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::max(30, std::min(300, settings.minBreathMs))));
    SendMessageW(state.minClick, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::max(1, std::min(20, settings.minClickMs))));
    SendMessageW(state.mergeGap, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::max(0, std::min(200, settings.mergeGapMs))));
    SendMessageW(state.detectBreath,BM_SETCHECK,settings.detectBreath?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(state.detectClicks,BM_SETCHECK,settings.detectClicks?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(state.pauseGate, BM_SETCHECK,
                 settings.requirePauseForBreath ? BST_CHECKED : BST_UNCHECKED, 0);
    updateSettingsLabels(state);
    invalidatePreview(state);
}

bool commitSettings(SettingsDialogState& state) {
    if (!state.settings) return false;
    const auto candidate = readControls(state);
    std::string error;
    if (!saveDetectorSettings(candidate, &error)) {
        MessageBoxW(GetParent(state.previewCanvas), toWide(error).c_str(),
                    L"Impostazioni non salvate", MB_OK | MB_ICONERROR);
        return false;
    }
    *state.settings = candidate;
    state.committed = true;
    invalidatePreview(state);
    return true;
}

std::string presetDirectory() {
    std::string directory = detectorSettingsDirectory() + "\\presets";
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(directory), ec);
    return directory;
}

std::string choosePresetPath(HWND owner, bool save) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Preset NL Breath Click Studio (*.bcpreset)\0*.bcpreset\0Tutti i file (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = save ? L"Salva preset di rilevamento" : L"Apri preset di rilevamento";
    dialog.lpstrDefExt = L"bcpreset";
    dialog.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST)
                        : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    const std::string directory = presetDirectory();
    const std::wstring wideDirectory = toWide(directory);
    dialog.lpstrInitialDir = wideDirectory.c_str();
    if (save) {
        wcscpy_s(path, MAX_PATH, L"mio-preset.bcpreset");
        if (!GetSaveFileNameW(&dialog)) return {};
    } else {
        if (!GetOpenFileNameW(&dialog)) return {};
    }
    return toUtf8(path);
}

LRESULT CALLBACK settingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsDialogState* state = g_settingsDialog;
    switch (message) {
        case WM_HSCROLL:
        case WM_COMMAND: {
            if (!state) return 0;
            const int id = LOWORD(wParam);
            if (id == kIdSetOk || id == kIdSetApply) {
                if (!commitSettings(*state)) return 0;
                if (id == kIdSetOk) {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            if (id == kIdSetCancel) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == kIdSetDefaults) {
                DetectorSettings defaults;
                setSlidersFromSettings(*state, defaults);
                return 0;
            }
            if (id == kIdSetSavePreset) {
                const std::string path = choosePresetPath(hwnd, true);
                if (!path.empty()) {
                    std::string error;
                    if (!saveDetectorPreset(path, readControls(*state), &error)) {
                        MessageBoxW(hwnd, toWide(error).c_str(), L"Preset",
                                    MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            if (id == kIdSetLoadPreset) {
                const std::string path = choosePresetPath(hwnd, false);
                if (!path.empty()) {
                    DetectorSettings loaded = readControls(*state);
                    std::string error;
                    if (loadDetectorPreset(path, loaded, &error)) {
                        setSlidersFromSettings(*state, loaded);
                    } else {
                        MessageBoxW(hwnd, toWide(error).c_str(), L"Preset",
                                    MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            for (int i = 0; i < 3; ++i) {
                if (id == kIdSetPresetConservative + i) {
                    DetectorSettings preset = readControls(*state);
                    applySensitivityPreset(preset, static_cast<SensitivityPreset>(i));
                    setSlidersFromSettings(*state, preset);
                    return 0;
                }
            }
            if (id == kIdPreviewPlay || id == kIdPreviewOriginal) {
                if (state->previewPlayer.isPlaying()) {
                    state->previewPlayer.stop();
                    SetWindowTextW(state->previewPlay, L"Ascolta Dopo");
                    return 0;
                }
                if (state->preview && state->preview->auditionSamples &&
                    !state->preview->auditionSamples->empty()) {
                    if (id == kIdPreviewPlay && (state->previewBusy || state->previewDirty ||
                                                !state->previewResult.error.empty())) return 0;
                    std::string audioError;
                    if (!state->previewPlayer.isOpen() &&
                        !state->previewPlayer.open(state->preview->sampleRate,
                                                   state->preview->channels, &audioError)) {
                        MessageBoxW(hwnd, toWide("Uscita audio non disponibile: " + audioError).c_str(),
                                    L"Anteprima audio", MB_OK | MB_ICONINFORMATION);
                        return 0;
                    }
                    std::shared_ptr<const std::vector<float>> audition = state->preview->auditionSamples;
                    if (id == kIdPreviewPlay) {
                        auto processed = std::make_shared<std::vector<float>>(*audition);
                        auto detections = state->previewResult.detections;
                        for (auto& detection : detections) {
                            detection.startSample -= state->preview->window.startFrame;
                            detection.endSample -= state->preview->window.startFrame;
                        }
                        RegionEditor::apply(*processed, state->preview->channels,
                                            state->preview->sampleRate, detections,
                                            state->preview->editSettings);
                        audition = processed;
                    }
                    state->previewPlayer.setAudio(audition,
                                                  static_cast<int64_t>(
                                                      state->preview->auditionSamples->size() /
                                                      std::max(1, state->preview->channels)));
                    state->previewPlayer.play(0);
                    SetWindowTextW(state->previewPlay, L"Ferma anteprima");
                }
                return 0;
            }
            updateSettingsLabels(*state);
            invalidatePreview(*state);
            return 0;
        }
        case WM_TIMER:
            if (state && wParam == kPreviewTimer) {
                KillTimer(hwnd, kPreviewTimer);
                if (state->previewDirty) updatePreview(*state);
            }
            if (state && wParam == kPreviewPollTimer && state->previewWorker) {
                if (!state->previewPlayer.isPlaying()) SetWindowTextW(state->previewPlay, L"Ascolta Dopo");
                if (state->previewWorker->takeResult(state->previewResult)) {
                    state->previewBusy = false;
                    presentPreview(*state);
                }
            }
            return 0;
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (state && draw && draw->hwndItem == state->previewCanvas &&
                state->preview && state->preview->document) {
                std::wstring counts = L"Anteprima locale";
                if (!state->previewResult.error.empty()) {
                    counts = L"Errore anteprima: " + toWide(state->previewResult.error);
                } else {
                    const DetectionPreviewDelta delta = compareDetectionProposals(
                        state->preview->before, state->previewResult.detections);
                    wchar_t buffer[160] = {};
                    swprintf(buffer, 160, L"mantenuti %d · aggiunti %d · rimossi %d",
                             delta.kept, delta.added, delta.removed);
                    counts = buffer;
                }
                drawDetectionPreview(draw->hDC, draw->rcItem, *state->preview->document,
                                     state->preview->window, state->preview->before,
                                     state->previewResult.detections,
                                     L"Prima: giallo mantenuto, rosso rimosso / Dopo: verde mantenuto, azzurro aggiunto", counts);
            } else if (draw) {
                FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_BTNFACE));
                SetBkMode(draw->hDC, TRANSPARENT);
                DrawTextW(draw->hDC, L"Apri un file per l'anteprima", -1,
                          const_cast<RECT*>(&draw->rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            return TRUE;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kPreviewTimer);
            KillTimer(hwnd, kPreviewPollTimer);
            if (state) {
                state->previewPlayer.stop();
                state->previewWorker.reset();
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_BTNTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        case WM_ERASEBKGND: {
            RECT client = {};
            GetClientRect(hwnd, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool showSettingsDialog(HWND parent, DetectorSettings& settings,
                        const DetectionPreviewContext* preview) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = settingsProc;
        windowClass.hInstance = GetModuleHandle(nullptr);
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        windowClass.lpszClassName = L"BreathClickSettingsDialog";
        RegisterClassExW(&windowClass);
        classRegistered = true;
    }

    SettingsDialogState state;
    state.settings = &settings;
    state.preview = preview;
    state.font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    g_settingsDialog = &state;

    const int width = 940;
    const int height = 660;
    RECT parentRect = {};
    GetWindowRect(parent, &parentRect);
    const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
    const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"BreathClickSettingsDialog",
                                  L"Impostazioni di rilevamento e anteprima",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, parent, nullptr,
                                  GetModuleHandle(nullptr), nullptr);
    if (!dialog) {
        DeleteObject(state.font);
        g_settingsDialog = nullptr;
        return false;
    }

    auto addLabel = [&](const wchar_t* text, int x0, int y0, int w0, int h0) {
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                       x0, y0, w0, h0, dialog, nullptr, nullptr, nullptr);
        setControlFont(control, state.font);
        return control;
    };
    addLabel(L"Profilo rapido (parametri reali, non etichette):", 18, 12, 430, 20);
    const wchar_t* presetNames[] = {L"Conservativo", L"Bilanciato", L"Alta sensibilità"};
    for (int i = 0; i < 3; ++i) {
        state.presetButtons[i] = CreateWindowExW(
            0, L"BUTTON", presetNames[i], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            18 + i * 140, 34, 132, 28, dialog,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSetPresetConservative + i)),
            nullptr, nullptr);
        setControlFont(state.presetButtons[i], state.font);
    }

    int cursorY = 76;
    auto addSlider = [&](int id, const wchar_t* label, HWND* slider, HWND* value,
                         int low, int high, int position) {
        addLabel(label, 18, cursorY, 300, 18);
        *value = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                 330, cursorY, 100, 18, dialog, nullptr, nullptr, nullptr);
        setControlFont(*value, state.font);
        *slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                  18, cursorY + 20, 412, 24, dialog,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessageW(*slider, TBM_SETRANGE, TRUE, MAKELPARAM(low, high));
        SendMessageW(*slider, TBM_SETPOS, TRUE, position);
        cursorY += 54;
    };

    addSlider(kIdSetBreathThreshold, L"Soglia respiri", &state.breathThreshold, &state.breathValue,
              0, kSliderResolution, static_cast<int>(settings.breathThreshold * kSliderResolution));
    addSlider(kIdSetClickThreshold, L"Soglia click", &state.clickThreshold, &state.clickValue,
              0, kSliderResolution, static_cast<int>(settings.clickThreshold * kSliderResolution));
    addSlider(kIdSetBreathSensitivity, L"Sensibilita' respiri", &state.breathSensitivity,
              &state.breathSensValue, 0, kSliderResolution,
              static_cast<int>(settings.breathSensitivity * kSliderResolution));
    addSlider(kIdSetClickSensitivity, L"Sensibilita' click", &state.clickSensitivity,
              &state.clickSensValue, 0, kSliderResolution,
              static_cast<int>(settings.clickSensitivity * kSliderResolution));
    addSlider(kIdSetMinBreath, L"Durata minima respiro", &state.minBreath,
              &state.minBreathValue, 30, 300,
              std::max(30, std::min(300, settings.minBreathMs)));
    addSlider(kIdSetMinClick, L"Durata minima click", &state.minClick,
              &state.minClickValue, 1, 20,
              std::max(1, std::min(20, settings.minClickMs)));
    addSlider(kIdSetMerge, L"Unione candidati (gap)", &state.mergeGap,
              &state.mergeValue, 0, 200,
              std::max(0, std::min(200, settings.mergeGapMs)));

    state.pauseGate = CreateWindowExW(
        0, L"BUTTON", L"I respiri devono stare in una pausa (non dentro una parola)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, cursorY, 412, 22, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSetPauseGate)), nullptr, nullptr);
    setControlFont(state.pauseGate, state.font);
    SendMessageW(state.pauseGate, BM_SETCHECK,
                 settings.requirePauseForBreath ? BST_CHECKED : BST_UNCHECKED, 0);
    cursorY += 34;
    addLabel(L"OK/Applica aggiornano progetto e predefiniti globali.\nI preset sono file separati; riaprire un progetto ripristina i suoi valori.",
             18, cursorY, 430, 55);

    state.previewCanvas = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        470, 16, 450, 390, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPreviewCanvas)), nullptr, nullptr);
    state.previewStatus = CreateWindowExW(0, L"STATIC", L"Anteprima in preparazione...",
                                          WS_CHILD | WS_VISIBLE, 470, 414, 450, 40, dialog,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPreviewStatus)),
                                          nullptr, nullptr);
    setControlFont(state.previewStatus, state.font);
    addLabel(L"L'anteprima analizza solo la finestra locale: non modifica il progetto.",
             470, 458, 450, 40);
    state.previewPlay = CreateWindowExW(0, L"BUTTON", L"Ascolta Dopo",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        470, 500, 150, 28, dialog,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPreviewPlay)),
                                        nullptr, nullptr);
    setControlFont(state.previewPlay, state.font);
    SetWindowTextW(state.previewPlay, L"Ascolta Dopo");
    state.previewOriginal = CreateWindowExW(0, L"BUTTON", L"Ascolta Prima",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 630, 500, 150, 28, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPreviewOriginal)), nullptr, nullptr);
    setControlFont(state.previewOriginal, state.font);
    addLabel(L"Anteprima DSP: Dopo applica l'attenuazione e la riparazione click correnti.",
             470, 540, 450, 40);

    state.detectBreath=CreateWindowExW(0,L"BUTTON",L"Rileva respiri",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
        470,555,200,24,dialog,reinterpret_cast<HMENU>(3060),nullptr,nullptr);
    state.detectClicks=CreateWindowExW(0,L"BUTTON",L"Rileva click / bocca",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
        690,555,220,24,dialog,reinterpret_cast<HMENU>(3061),nullptr,nullptr);
    SendMessageW(state.detectBreath,BM_SETCHECK,settings.detectBreath?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(state.detectClicks,BM_SETCHECK,settings.detectClicks?BST_CHECKED:BST_UNCHECKED,0);
    setControlFont(state.detectBreath,state.font);setControlFont(state.detectClicks,state.font);

    const int buttonY = 618;
    auto addButton = [&](int id, const wchar_t* text, int x0, int w0) {
        HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      x0, buttonY, w0, 28, dialog,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      nullptr, nullptr);
        setControlFont(button, state.font);
        return button;
    };
    addButton(kIdSetDefaults, L"Ripristina predefiniti", 18, 140);
    addButton(kIdSetSavePreset, L"Salva preset...", 166, 110);
    addButton(kIdSetLoadPreset, L"Carica preset...", 284, 110);
    addButton(kIdSetApply, L"Applica", 510, 86);
    addButton(kIdSetCancel, L"Annulla", 604, 86);
    addButton(kIdSetOk, L"OK", 698, 86);

    updateSettingsLabels(state);
    state.previewDirty = true;

    EnableWindow(parent, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetFocus(dialog);

    // La prima anteprima viene preparata dopo la visualizzazione: la finestra
    // appare subito e l'analisi locale non ritarda la creazione dei controlli.
    if (preview && preview->auditionSamples) {
        const auto& samples = *preview->auditionSamples;
        const int channels = std::max(1, preview->channels);
        std::vector<float> mono(samples.size() / channels, 0.0f);
        for (size_t f = 0; f < mono.size(); ++f) {
            for (int c = 0; c < channels; ++c) mono[f] += samples[f * channels + c];
            mono[f] /= channels;
        }
        state.previewWorker = std::make_unique<DetectionPreviewWorker>(
            std::move(mono), preview->sampleRate, preview->window.startFrame);
    }
    SetTimer(dialog, kPreviewTimer, 80, nullptr);
    SetTimer(dialog, kPreviewPollTimer, 30, nullptr);

    MSG message = {};
    while (IsWindow(dialog)) {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            if (!IsDialogMessageW(dialog, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            WaitMessage();
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (state.font) DeleteObject(state.font);
    g_settingsDialog = nullptr;
    if (state.committed) settings = *state.settings;
    return state.committed;
}

// ---------------------------------------------------------------------------
//  Menu
// ---------------------------------------------------------------------------
struct MenuEntry {
    UINT flags;
    UINT_PTR id;
    const wchar_t* text;
};

HMENU buildMenu(const MenuEntry* entries, size_t count) {
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < count; ++i) {
        AppendMenuW(menu, entries[i].flags, entries[i].id, entries[i].text);
    }
    return menu;
}

} // namespace

namespace bc {

// ===========================================================================
//  EditorWindow
// ===========================================================================
EditorWindow::EditorWindow() = default;

EditorWindow::~EditorWindow() {
    player_.close();
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    if (uiFont_) DeleteObject(uiFont_);
}

bool EditorWindow::create(HINSTANCE instance) {
    instance_ = instance;
    {
        std::string profileError;
        const std::string profilePath =
            detectorSettingsDirectory() + "\\personal_profile.ini";
        loadPersonalProfile(profilePath, personalProfile_, &profileError);
    }

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    backgroundBrush_ = CreateSolidBrush(RGB(34, 38, 44));
    uiFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &EditorWindow::windowProc;
    windowClass.hInstance = instance;
    // L'icona dell'applicazione, presa dalla risorsa 1 del modulo. Se per
    // qualsiasi motivo non ci fosse, si ripiega su quella di sistema invece di
    // lasciare la finestra senza icona.
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!windowClass.hIcon) {
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = backgroundBrush_;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!windowClass.hIconSm) {
        windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    }

    if (!RegisterClassExW(&windowClass)) {
        return false;
    }

    window_ = CreateWindowExW(0, kWindowClass, L"NL Breath Click Studio " BC_STUDIO_VERSION_W,
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1180, 820,
                              nullptr, nullptr, instance, this);
    if (!window_) {
        return false;
    }

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    return true;
}

LRESULT CALLBACK EditorWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    EditorWindow* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<EditorWindow*>(create->lpCreateParams);

        // La finestra va registrata SUBITO, qui.
        //
        // WM_CREATE arriva durante CreateWindowExW, cioe' PRIMA che il
        // chiamante possa assegnare il valore di ritorno a window_. Senza questa
        // riga createChildren() lavora con window_ ancora nullo: pulsanti,
        // barra di stato e vista della forma d'onda vengono creati con genitore
        // nullo, quindi diventano finestre di primo livello che vagano sullo
        // schermo invece di stare dentro la finestra.
        //
        // Sintomo: la finestra si apre, il titolo e' corretto, e dentro e' vuota.
        self->window_ = hwnd;

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handleMessage(message, wParam, lParam);
}

void EditorWindow::createChildren() {
    auto makeButton = [this](int id, const wchar_t* text, int x, int width) {
        HWND button = CreateWindowExW(0, L"BUTTON", text,
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      x, 7, width, 26, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      instance_, nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return button;
    };

    // ATTENZIONE: questi pulsanti devono usare gli ID di EditorCommand, non
    // quelli della barra. WM_COMMAND arriva con l'ID del pulsante e onCommand()
    // riconosce solo gli EditorCommand: con un ID diverso ogni clic cade nel
    // `default:` e non succede assolutamente nulla, senza alcun errore.
    openButton_ = makeButton(EditorCommand::kFileOpen, L"Apri...", 8, 84);
    saveButton_ = makeButton(EditorCommand::kFileSaveAs, L"Salva con nome...", 96, 130);
    exportButton_ = makeButton(EditorCommand::kFileExport, L"Esporta...", 230, 96);

    analyzeButton_ = makeButton(EditorCommand::kAnalyze, L"Analizza", 350, 96);
    settingsButton_ = makeButton(EditorCommand::kAnalyzeSettings, L"Impostazioni...", 450, 118);

    playButton_ = makeButton(EditorCommand::kTransportPlay, L"Riproduci", 596, 96);
    stopButton_ = makeButton(EditorCommand::kTransportStop, L"Stop", 696, 60);

    gainLabel_ = CreateWindowExW(0, L"STATIC", L"Guadagno generale", WS_CHILD | WS_VISIBLE,
                                 780, 4, 150, 16, window_, nullptr, instance_, nullptr);
    SendMessageW(gainLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

    gainValue_ = CreateWindowExW(0, L"STATIC", L"0.0 dB", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                 1050, 4, 90, 16, window_, nullptr, instance_, nullptr);
    SendMessageW(gainValue_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

    gainSlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                  780, 20, 360, 20, window_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdGain)),
                                  instance_, nullptr);
    // Da -24 dB a 0 dB, con lo zero in cima.
    SendMessageW(gainSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 240));
    SendMessageW(gainSlider_, TBM_SETPOS, TRUE, 0);

    statusBar_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                 8, 0, 400, 18, window_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatus)),
                                 instance_, nullptr);
    SendMessageW(statusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

    view_.create(window_, kIdWaveform, instance_);
    createFileListPanel();
    createRegionPanel();
    createTooltips();

    // La vista notifica la finestra quando cambia qualcosa che va riflesso
    // nella barra di stato o nel titolo.
    view_.onSelectionChanged = [this]() { updateStatusBar(); updateCommandState(); };
    view_.onRegionSelected = [this](int) {
        refreshRegionPanel(true);
        updateStatusBar();
        updateCommandState();
    };
    view_.onRegionChanged = [this](int) {
        // Durante il drag il pannello non viene ricostruito a ogni pixel: la
        // regione si sta ancora muovendo e il pannello verrebbe riletto per
        // nulla. Al rilascio del mouse il callback arriva con drag concluso.
        if (!view_.isDraggingRegion()) refreshRegionPanel(true);
        markDocumentChanged(false);
        updateStatusBar();
    };
    view_.onCursorMoved = [this](int64_t frame) {
        view_.setPlayhead(frame);
        updateStatusBar();
    };
    view_.onViewChanged = [this]() {
        if (player_.isPlaying() && followPlayhead_ && !followAutoScroll_) {
            followPlayhead_ = false;
            setStatusMessage("segui cursore disattivato: vista spostata manualmente");
            updateCommandState();
        }
        updateStatusBar();
    };
    view_.onDoubleClick = [this](int64_t frame) {
        // Doppio clic: porta la testina li' e suona da quel punto.
        view_.setPlayhead(frame);
        if (studioMode_) { view_.clearSelection(); commandPlayMode(abOriginal_); return; }
        if (!document_.isLoaded()) return;
        if (!ensurePlayerOpen()) return;
        renderIfNeeded(true);
        player_.play(frame);
        updateCommandState();
    };
}

void EditorWindow::createMenus() {
    mainMenu_ = CreateMenu();

    const MenuEntry fileEntries[] = {
        {MF_STRING, EditorCommand::kFileOpen, L"Apri...\tCtrl+O"},
        {MF_STRING, EditorCommand::kFileOpenInTrack, L"Apri come nuova traccia..."},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kFileSaveAs, L"Salva con nome...\tCtrl+S"},
        {MF_STRING, EditorCommand::kFileExport, L"Esporta audio corretto...\tCtrl+E"},
        {MF_STRING, EditorCommand::kFileExportDataset, L"Esporta correzioni per l'addestramento (accanto al file)\tCtrl+Shift+E"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kFileSaveSession, L"Salva sessione..."},
        {MF_STRING, EditorCommand::kFileOpenSession, L"Apri sessione..."},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kFileExit, L"Esci"}};


    const MenuEntry projectEntries[] = {
        {MF_STRING, EditorCommand::kProjectOpenFolder, L"Apri cartella di WAV/MP3/FLAC..."},
        {MF_STRING, EditorCommand::kProjectOpen, L"Apri progetto..."},
        {MF_STRING, EditorCommand::kProjectSave, L"Salva progetto"},
        {MF_STRING, EditorCommand::kProjectSaveAs, L"Salva progetto con nome..."},
        {MF_STRING, EditorCommand::kProjectRecover, L"Riprendi autosalvataggio"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kProjectAnalyzeAll, L"Analizza tutti i file"},
        {MF_STRING, EditorCommand::kProjectMarkReviewed, L"Reimposta inclusi tutti gli eventi del file"},
        {MF_STRING, EditorCommand::kProjectApproveAllFiles, L"Includi tutti gli eventi di tutti i file"},
        {MF_STRING, EditorCommand::kProjectExportAll, L"Elabora ed esporta tutti i file..."},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kProjectPreviousFile, L"File precedente\tCtrl+PagSu"},
        {MF_STRING, EditorCommand::kProjectNextFile, L"File successivo\tCtrl+PagGiù"}};

    const MenuEntry editEntries[] = {
        {MF_STRING, EditorCommand::kToolRegions, L"Strumento REGIONI"},
        {MF_STRING, EditorCommand::kToolTimeSelection, L"Strumento SELEZIONE TEMPORALE"},
        {MF_STRING, EditorCommand::kTimeSelectionBounds, L"Modifica confini della selezione..."},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kManualAttenuate, L"Attenua selezione (dB e fade)..."},
        {MF_STRING, EditorCommand::kManualSilence, L"Sostituisci selezione con silenzio"},
        {MF_STRING, EditorCommand::kManualCut, L"Taglia selezione (piano non distruttivo)"},
        {MF_STRING, EditorCommand::kManualHalve, L"Dimezza durata della selezione"},
        {MF_STRING, EditorCommand::kManualRestore, L"Ripristina interventi manuali nell'intervallo"},
        {MF_STRING, EditorCommand::kEditUndo, L"Annulla\tCtrl+Z"},
        {MF_STRING, EditorCommand::kEditRedo, L"Ripristina\tCtrl+Y"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kEditSelectAll, L"Seleziona tutto\tCtrl+A"},
        {MF_STRING, EditorCommand::kEditDeselect, L"Deseleziona\tEsc"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kEditCut, L"Taglia (togli e richiudi)\tCtrl+X"},
        {MF_STRING, EditorCommand::kEditDeleteSelection, L"Escludi evento / elimina selezione audio\tCanc"},
        {MF_STRING, EditorCommand::kEditSilenceSelection, L"Silenzia selezione"},
        {MF_STRING, EditorCommand::kEditCropToSelection, L"Ritaglia sulla selezione"},
        {MF_STRING, EditorCommand::kEditInsertSilence, L"Inserisci silenzio (0,5 s)"}};

    const MenuEntry detectEntries[] = {
        {MF_STRING, EditorCommand::kAnalyze, L"Analizza\tCtrl+R"},
        {MF_STRING, EditorCommand::kAnalyzeSettings, L"Impostazioni di rilevamento..."},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kClearRegions, L"Cancella tutte le regioni"},
        {MF_STRING, EditorCommand::kAnalyzeImportAi, L"Importa regioni AI..."},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kStudioDetectorDsp, L"Metodo: DSP"},
        {MF_STRING, EditorCommand::kStudioDetectorAi, L"Metodo: Respiro-en (AI)"},
        {MF_STRING, EditorCommand::kStudioDetectorFusion, L"Metodo: fusione DSP + AI (sperimentale)"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kStudioPreview, L"Anteprima del risultato"},
        {MF_STRING, EditorCommand::kPreviewResidual, L"Ascolta solo rimosso (differenza)"},
        {MF_STRING, EditorCommand::kStudioApply, L"Applica ed esporta il risultato..."},
        {MF_STRING, EditorCommand::kStudioExportPlan, L"Esporta piano versionato..."},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kStudioAttenuationSettings, L"Attenuazione e fade..."}};

    const MenuEntry regionEntries[] = {
        {MF_STRING, EditorCommand::kRegionsInTimeSelection, L"Seleziona eventi nell'intervallo temporale"},
        {MF_STRING, EditorCommand::kClicksDisable, L"Escludi tutti i click"},
        {MF_STRING, EditorCommand::kClicksEnable, L"Reincludi tutti i click"},
        {MF_STRING, EditorCommand::kRegionAddAtSelection, L"Aggiungi regione sulla selezione"},
        {MF_STRING, EditorCommand::kRegionConfirm, L"Includi evento"},
        {MF_STRING, EditorCommand::kRegionReject, L"Escludi evento (resta nel progetto)"},
        {MF_STRING, EditorCommand::kRegionAudition, L"Ascolta l'evento selezionato"},
        {MF_STRING, EditorCommand::kRegionNext, L"Evento successivo"},
        {MF_STRING, EditorCommand::kRegionPrevious, L"Evento precedente"},
        {MF_STRING, EditorCommand::kRegionSelectAll, L"Seleziona tutti gli eventi"},
        {MF_STRING, EditorCommand::kRegionClearSelection, L"Deseleziona eventi"},
        {MF_STRING, EditorCommand::kRegionApproveAll, L"Includi tutti gli eventi"},
        {MF_STRING, EditorCommand::kRegionRejectAll, L"Escludi tutti gli eventi"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kRegionModeAttenuate, L"Modalita': Attenua"},
        {MF_STRING, EditorCommand::kRegionModeCut, L"Modalita': Taglia"},
        {MF_STRING, EditorCommand::kRegionModeHalve, L"Modalita': Dimezza"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kRegionGlobalAttenuate, L"Modalita' globale: Attenua tutti gli inclusi"},
        {MF_STRING, EditorCommand::kRegionGlobalCut, L"Modalita' globale: Taglia tutti gli inclusi"},
        {MF_STRING, EditorCommand::kRegionGlobalHalve, L"Modalita' globale: Dimezza tutti gli inclusi"},
        {MF_STRING, EditorCommand::kRegionKeepFraction, L"Percentuale conservata (Dimezza)..."},
        {MF_STRING, EditorCommand::kRegionKeep25, L"Dimezza: conserva 25%"},
        {MF_STRING, EditorCommand::kRegionKeep50, L"Dimezza: conserva 50%"},
        {MF_STRING, EditorCommand::kRegionKeep75, L"Dimezza: conserva 75%"},
        {MF_STRING, EditorCommand::kRegionMargin0, L"Margine sicurezza: 0 ms"},
        {MF_STRING, EditorCommand::kRegionMargin10, L"Margine sicurezza: 10 ms"},
        {MF_STRING, EditorCommand::kRegionMargin20, L"Margine sicurezza: 20 ms"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kRegionDelete, L"Elimina definitivamente la regione"},
        {MF_STRING, EditorCommand::kRegionToggleEnabled, L"Includi / escludi regione"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kRegionSetBreath, L"Tipo: respiro"},
        {MF_STRING, EditorCommand::kRegionSetClick, L"Tipo: click"},
        {MF_STRING, EditorCommand::kRegionRepairToggle, L"Ripara click / attenua"},
        {MF_STRING, EditorCommand::kClickRepairAuto, L"De-click: automatico"},
        {MF_STRING, EditorCommand::kClickRepairLinear, L"De-click: interpolazione lineare"},
        {MF_STRING, EditorCommand::kClickRepairPredictive, L"De-click: ricostruzione predittiva"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kRegionGainDown, L"Attenua di piu' (3 dB)"},
        {MF_STRING, EditorCommand::kRegionGainUp, L"Attenua di meno (3 dB)"},
        {MF_STRING, EditorCommand::kRegionGainReset, L"Riporta a -18 dB"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kRegionShortenHalf, L"Dimezza i fiati (taglia e richiudi)"},
        {MF_STRING, EditorCommand::kRegionShortenThird, L"Riduci i fiati a un terzo"},
        {MF_STRING, EditorCommand::kRegionShortenQuarter, L"Riduci i fiati a un quarto"}};

    const MenuEntry viewEntries[] = {
        {MF_STRING, EditorCommand::kViewZoomIn, L"Ingrandisci\t+"},
        {MF_STRING, EditorCommand::kViewZoomOut, L"Riduci\t-"},
        {MF_STRING, EditorCommand::kViewZoomFit, L"Adatta alla finestra"},
        {MF_STRING, EditorCommand::kViewZoomSelection, L"Zoom sulla selezione"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kTransportPlay, L"Riproduci / Pausa\tSpazio"},
        {MF_STRING, EditorCommand::kTransportPlaySelection, L"Riproduci la selezione"},
        {MF_STRING, EditorCommand::kTransportPlayOriginal, L"Ascolta ORIGINALE\tCtrl+1"},
        {MF_STRING, EditorCommand::kTransportPlayProcessed, L"Ascolta ELABORATO\tCtrl+2"},
        {MF_STRING, EditorCommand::kTransportStop, L"Ferma"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kGoToStart, L"Vai all'inizio\tHome"},
        {MF_STRING, EditorCommand::kGoToEnd, L"Vai alla fine\tFine"},
        {MF_STRING, EditorCommand::kGoToSelectedRegion, L"Vai al respiro selezionato\tG"},
        {MF_STRING, EditorCommand::kViewGoPlayhead, L"Vai alla posizione di riproduzione"},
        {MF_STRING, EditorCommand::kViewFollowPlayhead, L"Segui il cursore durante la riproduzione"},
        {MF_STRING, EditorCommand::kViewVerticalReset, L"Ripristina ampiezza 1x"}};

    const MenuEntry learningEntries[] = {
        {MF_STRING, EditorCommand::kLearningConfirmBreath, L"Conferma respiro selezionato per il training"},
        {MF_STRING, EditorCommand::kLearningMissedBreath, L"Segna come respiro mancato confermato"},
        {MF_STRING, EditorCommand::kLearningFalsePositive, L"Segna come falso positivo confermato"},
        {MF_STRING, EditorCommand::kLearningEditorialExclusion, L"Segna esclusione editoriale (non training)"},
        {MF_STRING, EditorCommand::kLearningClickConfirmed, L"Conferma click"},
        {MF_STRING, EditorCommand::kLearningClickFalsePositive, L"Falso positivo click"},
        {MF_STRING, EditorCommand::kLearningMissedClick, L"Click mancato (regione manuale)"},
        {MF_STRING, EditorCommand::kLearningMouthConfirmed, L"Conferma schiocco saliva/labbra"},
        {MF_STRING, EditorCommand::kLearningVocalTransient, L"Transiente vocale da preservare"},
        {MF_STRING, EditorCommand::kLearningUnverified, L"Riporta a non verificato"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kLearningTrain, L"Impara dalle correzioni..."},
        {MF_STRING, EditorCommand::kLearningActivate, L"Attiva/disattiva profilo personale"},
        {MF_STRING, EditorCommand::kLearningReset, L"Ripristina profilo generale"},
        {MF_STRING, EditorCommand::kLearningStatus, L"Stato del profilo..."}};

    const MenuEntry fadeEntries[] = {
        {MF_STRING, EditorCommand::kFadeSet, L"Applica fade di ingresso e uscita (50 ms)"},
        {MF_STRING, EditorCommand::kFadeClear, L"Rimuovi i fade"}};

    const MenuEntry helpEntries[] = {
        {MF_STRING, EditorCommand::kHelpShortcuts, L"Tasti e scorciatoie\tF1"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kHelpAbout, L"Informazioni su NL Breath Click Studio"}};

    const MenuEntry exportEntries[] = {
        {MF_STRING, EditorCommand::kExportFormatSame, L"Stesso formato dell'originale (default)"},
        {MF_STRING, EditorCommand::kExportFormatWav, L"WAV PCM 24 bit"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kExportMp3128, L"MP3 CBR 128 kbps"},
        {MF_STRING, EditorCommand::kExportMp3192, L"MP3 CBR 192 kbps"},
        {MF_STRING, EditorCommand::kExportMp3256, L"MP3 CBR 256 kbps"},
        {MF_STRING, EditorCommand::kExportMp3320, L"MP3 CBR 320 kbps"},
        {MF_SEPARATOR, 0, nullptr},
        {MF_STRING, EditorCommand::kExportMp3VbrQ0, L"MP3 VBR Q0 (qualita' massima)"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ1, L"MP3 VBR Q1"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ2, L"MP3 VBR Q2"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ3, L"MP3 VBR Q3"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ4, L"MP3 VBR Q4"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ5, L"MP3 VBR Q5"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ6, L"MP3 VBR Q6"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ7, L"MP3 VBR Q7"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ8, L"MP3 VBR Q8"},
        {MF_STRING, EditorCommand::kExportMp3VbrQ9, L"MP3 VBR Q9 (qualita' minore)"}};
    HMENU fileMenu = buildMenu(fileEntries, std::size(fileEntries));
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(exportEntries, std::size(exportEntries))),
                L"Formato esportazione");
    AppendMenuW(mainMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"File");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(projectEntries,
                                                     std::size(projectEntries))),
                L"Progetto");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(editEntries,
                                                     std::size(editEntries))),
                L"Modifica");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(detectEntries,
                                                     std::size(detectEntries))),
                L"Rilevamento");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(regionEntries,
                                                     std::size(regionEntries))),
                L"Regione");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(learningEntries,
                                                     std::size(learningEntries))),
                L"Apprendimento");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(viewEntries,
                                                     std::size(viewEntries))),
                L"Vista");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(fadeEntries,
                                                     std::size(fadeEntries))),
                L"Fade");
    AppendMenuW(mainMenu_, MF_POPUP,
                reinterpret_cast<UINT_PTR>(buildMenu(helpEntries,
                                                     std::size(helpEntries))),
                L"Aiuto");

    // Lo stesso elenco delle regioni serve al menu contestuale del tasto
    // destro sulla forma d'onda. Si costruisce una volta sola e si riusa:
    // le voci sono identiche, e duplicarle significherebbe doverle
    // aggiornare in due posti.
    regionMenu_ = buildMenu(regionEntries, std::size(regionEntries));

    // Il menu delle tracce si riempie a runtime: il numero di voci dipende da
    // quanti file sono aperti, quindi non puo' stare in un array statico.
    trackMenu_ = CreatePopupMenu();
    AppendMenuW(mainMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(trackMenu_),
                L"Traccia");

    // Anche il menu dei profili e' dinamico: dipende da cosa c'e' nella
    // cartella dei modelli.
    modelMenu_ = CreatePopupMenu();
    AppendMenuW(mainMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(modelMenu_),
                L"Modello");

    SetMenu(window_, mainMenu_);

    rebuildTrackMenu();
    rebuildModelMenu();
}

// ---------------------------------------------------------------------------
//  Disposizione dei controlli
// ---------------------------------------------------------------------------
void EditorWindow::layoutChildren() {
    RECT client = {};
    GetClientRect(window_, &client);

    const int width = client.right;
    const int height = client.bottom;

    // Il cursore non deve finire sotto i pulsanti: Stop arriva a x=756
    // (596 + 96 + 4 + 60), e con 700 il cursore ci passava sopra. Si vedevano
    // due controlli sovrapposti e il clic sulla parte sinistra del cursore
    // poteva finire sul pulsante.
    const int gainLeft = std::max(770, width - 420);
    MoveWindow(gainLabel_, gainLeft, 4, 200, 16, TRUE);
    MoveWindow(gainValue_, gainLeft + 240, 4, 90, 16, TRUE);
    MoveWindow(gainSlider_, gainLeft, 18, 340, 22, TRUE);

    const int waveTop = kToolbarHeight;
    const int waveHeight = std::max(40, height - kToolbarHeight - kStatusHeight - kLegendHeight);
    layoutFileListPanel();
    layoutRegionPanel();
    RECT listRect = {};
    int listWidth = 0;
    if (fileList_ && GetWindowRect(fileList_, &listRect)) listWidth = listRect.right - listRect.left;
    int panelWidth = 0;
    if (regionPanel_) {
        RECT panelRect = {};
        if (GetWindowRect(regionPanel_, &panelRect)) panelWidth = panelRect.right - panelRect.left;
    }
    MoveWindow(view_.handle(), listWidth, waveTop,
               std::max(80, width - listWidth - panelWidth), waveHeight, TRUE);
    MoveWindow(statusBar_, 8, height - kStatusHeight + 2, std::max(100, width - 16), 18, TRUE);
}

// ---------------------------------------------------------------------------
//  Ciclo dei messaggi
// ---------------------------------------------------------------------------
int EditorWindow::runMessageLoop() {
    // ---------------------------------------------------------------------
    //  TABELLA DEGLI ACCELERATORI
    //
    //  I tasti sono gestiti anche dentro la vista onda, che li riceve pero'
    //  soltanto se ha il fuoco. Dopo aver premuto un pulsante il fuoco sta
    //  sul pulsante, quindi Ctrl+A, Canc e spazio non facevano NULLA:
    //  l'utente premeva i tasti e non succedeva niente.
    //
    //  La tabella li rende validi da qualunque punto della finestra. E' il
    //  modo previsto da Windows, e non dipende da dove sta il fuoco.
    // ---------------------------------------------------------------------
    ACCEL acceleratori[] = {
        {FVIRTKEY | FCONTROL, 'A', EditorCommand::kEditSelectAll},
        {FVIRTKEY | FCONTROL, 'X', EditorCommand::kEditCut},
        {FVIRTKEY, VK_DELETE, EditorCommand::kEditDeleteSelection},
        {FVIRTKEY | FCONTROL, 'Z', EditorCommand::kEditUndo},
        {FVIRTKEY | FCONTROL | FSHIFT, 'Z', EditorCommand::kEditRedo},
        {FVIRTKEY | FCONTROL, 'Y', EditorCommand::kEditRedo},
        {FVIRTKEY | FCONTROL, 'S', EditorCommand::kFileSaveAs},
        {FVIRTKEY | FCONTROL, 'O', EditorCommand::kFileOpen},
        {FVIRTKEY | FCONTROL, 'E', EditorCommand::kFileExport},
        {FVIRTKEY | FCONTROL, 'R', EditorCommand::kAnalyze},
        {FVIRTKEY, VK_SPACE, EditorCommand::kTransportPlay},
        {FVIRTKEY, VK_ESCAPE, EditorCommand::kEditDeselect},
        {FVIRTKEY, VK_F1, EditorCommand::kHelpShortcuts},
        {FVIRTKEY, VK_F5, EditorCommand::kAnalyze},
        {FVIRTKEY, VK_F2, EditorCommand::kAnalyzeSettings},
        {FVIRTKEY | FCONTROL | FSHIFT, 'E', EditorCommand::kFileExportDataset},
        {FVIRTKEY | FCONTROL | FSHIFT, 'R', EditorCommand::kModelReload},
        {FVIRTKEY | FCONTROL | FSHIFT, 'B', EditorCommand::kModelResetToBase},
        {FVIRTKEY | FCONTROL | FSHIFT, 'O', EditorCommand::kProjectOpenFolder},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', EditorCommand::kProjectSave},
        {FVIRTKEY | FCONTROL, VK_PRIOR, EditorCommand::kProjectPreviousFile},
        {FVIRTKEY | FCONTROL, VK_NEXT, EditorCommand::kProjectNextFile},
        {FVIRTKEY, 'A', EditorCommand::kRegionConfirm},
        {FVIRTKEY, 'R', EditorCommand::kRegionReject},
        {FVIRTKEY, 'L', EditorCommand::kRegionAudition},
        {FVIRTKEY, 'N', EditorCommand::kRegionNext},
        {FVIRTKEY, 'P', EditorCommand::kRegionPrevious},
        {FVIRTKEY, 'G', EditorCommand::kGoToSelectedRegion},
    };

    HACCEL tabella = CreateAcceleratorTableW(
        acceleratori, static_cast<int>(std::size(acceleratori)));

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        // TranslateAcceleratorW intercetta la combinazione e la manda come
        // WM_COMMAND alla finestra: la scorciatoia funziona anche se il fuoco
        // e' su un pulsante o su un campo di testo.
        if (tabella && TranslateAcceleratorW(window_, tabella, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (tabella) {
        DestroyAcceleratorTable(tabella);
    }
    return static_cast<int>(message.wParam);
}

// ---------------------------------------------------------------------------
//  Messaggi della finestra
// ---------------------------------------------------------------------------
LRESULT EditorWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    // Messaggi di sola diagnosi per i test GUI: non modificano lo stato.
    switch (message) {
        case WM_APP + 301: return static_cast<LRESULT>(document_.regions().size());
        case WM_APP + 302: {
            const int index = static_cast<int>(wParam);
            return index >= 0 && index < static_cast<int>(document_.regions().size())
                ? static_cast<LRESULT>(document_.regions()[static_cast<size_t>(index)].startSample) : -1;
        }
        case WM_APP + 303: {
            const int index = static_cast<int>(wParam);
            return index >= 0 && index < static_cast<int>(document_.regions().size())
                ? static_cast<LRESULT>(document_.regions()[static_cast<size_t>(index)].endSample) : -1;
        }
        case WM_APP + 304: {
            const int index = static_cast<int>(wParam);
            return index >= 0 && index < static_cast<int>(document_.regions().size()) &&
                   document_.regions()[static_cast<size_t>(index)].enabled ? 1 : 0;
        }
        case WM_APP + 305: {
            const int index = static_cast<int>(wParam);
            return index >= 0 && index < static_cast<int>(document_.regions().size()) &&
                   document_.regions()[static_cast<size_t>(index)].approved ? 1 : 0;
        }
        case WM_APP + 306: return static_cast<LRESULT>(view_.selectedRegion());
        case WM_APP + 307: return static_cast<LRESULT>(document_.undoDepth());
        case WM_APP + 308: return static_cast<LRESULT>(view_.visibleStartFrame());
        case WM_APP + 309: return static_cast<LRESULT>(view_.visibleFrameCount());
        case WM_APP + 310: return static_cast<LRESULT>(view_.selectionStart());
        case WM_APP + 311: return static_cast<LRESULT>(view_.selectionEnd());
        case WM_APP + 316: return view_.isDraggingRegion() ? 1 : 0;
        case WM_APP + 317: return view_.dragMoved() ? 1 : 0;
        case WM_APP + 312: return static_cast<LRESULT>(view_.playhead());
        case WM_APP + 318: return followPlayhead_ ? 1 : 0;
        case WM_APP + 319: return static_cast<LRESULT>(view_.selectedRegions().size());
        case WM_APP + 320: return hasPreview_ ? 1 : 0;
        case WM_APP + 321: return hasPreview_ ? static_cast<LRESULT>(preview_.audio.frameCount()) : -1;
        case WM_APP + 322: {
            const size_t i = static_cast<size_t>(wParam) * document_.channels();
            return hasPreview_ && i < preview_.audio.samples.size() ? static_cast<LRESULT>(std::llround(preview_.audio.samples[i] * 1000000.)) : 0;
        }
        case WM_APP + 323: {
            const size_t i = static_cast<size_t>(wParam) * document_.channels();
            return i < document_.audio().samples.size() ? static_cast<LRESULT>(std::llround(document_.audio().samples[i] * 1000000.)) : 0;
        }
        case WM_APP + 325: {
            const auto index=static_cast<size_t>(wParam);
            return index<document_.regions().size()?static_cast<LRESULT>(document_.regions()[index].kind):-1;
        }
        case WM_APP + 324: return static_cast<LRESULT>(std::llround(player_.sampleAt(static_cast<int64_t>(wParam)) * 1000000.));
        default: break;
    }
    switch (message) {
        case WM_CREATE:
            createChildren();
            createMenus();
            loadModelProfiles();
            // I profili modello applicano i loro valori predefiniti: le
            // impostazioni confermate dall'utente devono avere la precedenza
            // e vanno ricaricate dopo, non prima, della loro inizializzazione.
            loadDetectorSettings(detectorSettings_, nullptr);
            rebuildModelMenu();
            // La testina si aggiorna spesso, il rendering differito meno: sono
            // due cose diverse e due timer diversi.
            SetTimer(window_, kTimerPlayhead, 40, nullptr);
            SetTimer(window_, kTimerRender, 180, nullptr);
            SetTimer(window_, kTimerAutosave, 10000, nullptr);
            refreshDeviceStatus();
            updateTitle();
            updateStatusBar();
            updateCommandState();
            layoutChildren();
            return 0;

        case WM_SIZE:
            layoutChildren();
            return 0;

        case WM_COMMAND:
            onCommand(static_cast<int>(LOWORD(wParam)));
            return 0;

        case WM_KEYDOWN: {
            // Fallback per quando il messaggio arriva direttamente alla
            // finestra (per esempio dopo un clic su un pulsante): i comandi di
            // revisione devono funzionare senza dipendere dal controllo che ha
            // il fuoco.
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (!control) {
                if (wParam == VK_DELETE && view_.selectedRegion() >= 0) {
                    commandDeleteSelection();
                    return 0;
                }
                if (wParam == 'A') { commandRegionDecision(true); return 0; }
                if (wParam == 'R') { commandRegionDecision(false); return 0; }
                if (wParam == 'L') { commandAuditionRegion(); return 0; }
                if (wParam == 'N') { commandRegionStep(1); return 0; }
                if (wParam == 'P') { commandRegionStep(-1); return 0; }
            }
            break;
        }

        case WM_HSCROLL:
            // Il cursore del guadagno manda WM_HSCROLL al genitore, non
            // WM_COMMAND. Si traduce in un comando per avere un solo percorso.
            if (reinterpret_cast<HWND>(lParam) == verticalZoomSlider_) {
                static constexpr double levels[] = {1.0, 2.0, 4.0, 8.0};
                const int position = static_cast<int>(SendMessageW(verticalZoomSlider_, TBM_GETPOS, 0, 0));
                const int index = std::max(0, std::min(3, position));
                view_.setVerticalZoom(levels[index]);
                if (verticalZoomValue_) {
                    wchar_t label[16] = {};
                    swprintf(label, 16, L"%.0fx", levels[index]);
                    SetWindowTextW(verticalZoomValue_, label);
                }
                return 0;
            }
            if (reinterpret_cast<HWND>(lParam) == gainSlider_) {
                const int posizione =
                    static_cast<int>(SendMessageW(gainSlider_, TBM_GETPOS, 0, 0));
                const float db = -static_cast<float>(posizione) / 10.0f;
                document_.setMasterGainDb(db);

                wchar_t etichetta[32] = {};
                swprintf(etichetta, 32, L"%.1f dB", static_cast<double>(db));
                SetWindowTextW(gainValue_, etichetta);

                markDocumentChanged(false);
            }
            return 0;

        case WM_TIMER:
            if (wParam == kTimerPlayhead) {
                if (player_.isPlaying()) {
                    const int64_t position = playerPositionInDocument();
                    view_.setPlayhead(position);
                    if (followPlayhead_) {
                        followAutoScroll_ = true;
                        view_.ensureFrameVisible(position);
                        followAutoScroll_ = false;
                    }
                }
                return 0;
            }
            if (wParam == kTimerRender) {
                renderIfNeeded(false);
                return 0;
            }
            if (wParam == kTimerAutosave) {
                if (recoveryPending_ && !tracks_.empty() && !view_.isDraggingRegion()) saveRecovery();
                return 0;
            }
            return 0;

        case WM_NOTIFY: {
            const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header && header->hwndFrom == fileList_ && header->code == LVN_ITEMCHANGED) {
                onFileListSelectionChanged();
                return 0;
            }
            break;
        }

        case WM_CONTEXTMENU: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            onContextMenu(x, y);
            return 0;
        }

        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT client = {};
            GetClientRect(window_, &client);
            FillRect(dc, &client, backgroundBrush_);
            return 1;
        }

        case WM_CLOSE:
            if (!tracks_.empty()) saveRecovery();
            // Si chiude senza domande.
            //
            // Chiedere conferma sembrerebbe piu' prudente, ma qui non c'e'
            // niente da perdere: l'audio originale non viene mai toccato, e
            // le regioni si rifanno con un'analisi. Una finestra di conferma
            // che compare a ogni chiusura e' solo un fastidio in piu'.
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
            KillTimer(window_, kTimerPlayhead);
            KillTimer(window_, kTimerRender);
            KillTimer(window_, kTimerAutosave);
            player_.stop();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
//  Titolo e barra di stato
// ---------------------------------------------------------------------------
void EditorWindow::updateTitle() {
    std::wstring titolo = L"NL Breath Click Studio " BC_STUDIO_VERSION_W;

    if (document_.isLoaded()) {
        titolo += L" - ";
        titolo += toWide(document_.displayName());

        // L'asterisco e' il segnale convenzionale di "modifiche non salvate".
        if (document_.isModified()) {
            titolo += L" *";
        }
    }

    // Con piu' tracce aperte si dice quale si sta guardando: senza, dopo un
    // cambio si perde il conto di dove si e' finiti.
    if (trackNames_.size() > 1) {
        titolo += L"   [traccia ";
        titolo += std::to_wstring(activeTrack_ + 1);
        titolo += L" di ";
        titolo += std::to_wstring(trackNames_.size());
        titolo += L"]";
    }

    SetWindowTextW(window_, titolo.c_str());
}

void EditorWindow::updateStatusBar() {
    // Il formato conta: i test leggono la durata da qui con
    // /(\d+\.\d+) s/ e verificano che il dimezzamento la riduca.
    std::string testo;

    if (!document_.isLoaded()) {
        testo = "Nessun file aperto";
        if (!statusMessage_.empty()) {
            testo += "  |  " + statusMessage_;
        }
        SetWindowTextW(statusBar_, toWide(testo).c_str());
        return;
    }

    testo += document_.displayName();
    // formatSeconds restituisce gia' "6.400 s": aggiungere " s" qui produceva
    // "6.400 s s" nella barra di stato.
    testo += "  |  " + formatSeconds(document_.durationSeconds());
    testo += "  |  " + std::to_string(document_.sampleRate()) + " Hz";
    testo += "  |  " + std::to_string(document_.channels()) + " canali";

    if (view_.hasSelection()) {
        testo += "  |  selezione ";
        testo += formatTimecode(static_cast<double>(view_.selectionStart()) /
                                std::max(1, document_.sampleRate()));
        testo += " - ";
        testo += formatTimecode(static_cast<double>(view_.selectionEnd()) /
                                std::max(1, document_.sampleRate()));
        testo += " (" +
                 formatSeconds(static_cast<double>(view_.selectionEnd() -
                                                  view_.selectionStart()) /
                               std::max(1, document_.sampleRate())) +
                 ")";
    }

    int respiri = 0;
    int click = 0;
    for (const EditRegion& regione : document_.regions()) {
        if (regione.kind == DetectionKind::Breath) {
            ++respiri;
        } else {
            ++click;
        }
    }

    testo += "  |  regioni: " + std::to_string(document_.regions().size());
    testo += "  |  " + std::to_string(respiri) + " respiri, " +
             std::to_string(click) + " click";
    if (studioMode_) {
        int inclusi = 0;
        const auto& provenance = document_.analysisProvenance();
        testo += "  |  analisi effettiva: " + (provenance.detector.empty() ? std::string("non documentata") : provenance.detector);
        int esclusi = 0;
        int applicati = 0;
        for (const EditRegion& regione : document_.regions()) {
            if (regione.enabled && regione.approved) ++inclusi;
            else ++esclusi;
            if (regione.applied) ++applicati;
        }
        testo += "  |  inclusi: " + std::to_string(inclusi) + "/" +
                 std::to_string(document_.regions().size()) +
                 "  |  esclusi: " + std::to_string(esclusi);
        if (applicati > 0) {
            testo += "  |  applicati: " + std::to_string(applicati);
        }
        if (engineApplied_ >= 0) testo += "  |  motore applicati: " + std::to_string(engineApplied_) + ", rifiutati: " + std::to_string(engineRejected_);
        const int index = view_.selectedRegion();
        if (index >= 0 && index < static_cast<int>(document_.regions().size())) {
            const EditRegion& regione = document_.regions()[static_cast<size_t>(index)];
            const char* modi[] = {"attenua", "taglia", "dimezza", "silenzia"};
            testo += "  |  evento: ";
            testo += modi[std::max(0, std::min(3, regione.editMode))];
            if (regione.editMode == 2) {
                char percent[32] = {};
                snprintf(percent, sizeof(percent), " %.0f%%", regione.keepFraction * 100.0);
                testo += percent;
            }
            testo += (regione.enabled && regione.approved) ? " (incluso)" : " (escluso)";
        }
    }

    // Il profilo attivo si vede sempre: cambiandolo si cambia il modo in cui
    // il programma riconosce gli eventi, e senza l'indicazione non si capisce
    // perche' due analisi dello stesso file danno risultati diversi.
    if (!activeProfile_.empty()) {
        testo += "  |  profilo: " + activeProfile_;
    }

    // Il modello va detto: due analisi dello stesso file danno risultati
    // diversi a seconda che il profilo applichi una rete o una retta.
    if (detectorSettings_.usaReteDaFile) {
        testo += "  |  rete del profilo";
    } else if (detectorSettings_.usaReteNeurale) {
        testo += "  |  rete compilata";
    } else if (detectorSettings_.useBreathModel) {
        testo += "  |  modello lineare";
    }

    if (!deviceStatus_.empty()) {
        testo += "  |  " + deviceStatus_;
    }
    if (!statusMessage_.empty()) {
        testo += "  |  " + statusMessage_;
    }

    SetWindowTextW(statusBar_, toWide(testo).c_str());
}

void EditorWindow::setStatusMessage(const std::string& message) {
    statusMessage_ = message;
    updateStatusBar();
}

void EditorWindow::refreshDeviceStatus() {
    // Il rilevamento e' DSP puro: nessun modello, nessuna GPU. La riga lo dice
    // esplicitamente perche' in passato il programma poteva usare ONNX e la
    // differenza contava.
    deviceStatus_ = studioMode_ ? "CPU; metodo richiesto: " + studioDetector_ : "CPU (solo DSP)";
}

void EditorWindow::markDocumentChanged(bool rebuildPeaks) {
    engineApplied_ = engineRejected_ = -1;
    if (playerUsesPreview_) { player_.stop(); playerUsesPreview_ = false; }
    hasPreview_ = false;
    documentDirty_ = true;
    projectDirty_ = true;
    recoveryPending_ = true;
    renderPending_ = true;
    // Durante un drag di regione la vista viene ridisegnata a ogni movimento,
    // ma non si ricostruiscono pannello, titolo, lista file e stato comandi:
    // sono le operazioni che rendevano il trascinamento scattoso. Al rilascio
    // del mouse arriva una notifica completa e lo stato viene riallineato.
    if (view_.isDraggingRegion()) {
        view_.invalidate();
        updateStatusBar();
        return;
    }
    if (fileList_) refreshTrackStatus();
    refreshRegionPanel();
    view_.refreshDocument(rebuildPeaks);
    updateTitle();
    updateCommandState();
    updateStatusBar();
}

void EditorWindow::renderIfNeeded(bool force) {
    if (!document_.isLoaded()) return;
    // Studio playback is rendered exclusively through EditPlan/TimeMap.
    // The legacy timer must never replace Original or Processed buffers.
    if (studioMode_) { renderPending_ = false; return; }
    if (!renderPending_ && !force) return;
    if (view_.isDraggingRegion()) return;

    // Sostituire il buffer mentre si suona provocherebbe un salto: si aspetta
    // che la riproduzione finisca o venga fermata.
    if (player_.isPlaying()) return;

    renderPending_ = false;

    auto rendered = std::make_shared<AudioBuffer>(document_.render(editSettings_));
    const int64_t frames = static_cast<int64_t>(rendered->samples.size()) /
                           std::max(1, rendered->channels);

    if (ensurePlayerOpen()) {
        // Il costruttore di alias tiene vivo il buffer completo finche' il
        // lettore usa i campioni: senza di esso il vettore verrebbe liberato
        // appena `rendered` esce di scena.
        player_.setAudio(std::shared_ptr<const std::vector<float>>(rendered,
                                                                  &rendered->samples),
                         frames);
        playerUsesPreview_ = false;
    }

    renderedAudio_ = std::shared_ptr<const std::vector<float>>(rendered, &rendered->samples);
    view_.refreshGainCurve();
}

bool EditorWindow::ensurePlayerOpen() {
    if (!document_.isLoaded()) return false;

    std::string error;
    if (!player_.open(document_.sampleRate(), std::min(2, document_.channels()), &error)) {
        static bool warned = false;
        if (!warned) {
            setStatusMessage("Riproduzione non disponibile: " + error);
            warned = true;
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Stato dei comandi
// ---------------------------------------------------------------------------
void EditorWindow::updateCommandState() {
    const bool caricato = document_.isLoaded();
    const bool selezione = caricato && view_.hasSelection();
    const bool modificabile = caricato;

    // I pulsanti della barra non sono voci di menu: vanno abilitati a mano,
    // altrimenti restano attivi anche senza file aperto e cliccarli non fa
    // nulla (o peggio).
    EnableWindow(openButton_, TRUE);
    EnableWindow(saveButton_, modificabile ? TRUE : FALSE);
    EnableWindow(exportButton_, modificabile ? TRUE : FALSE);
    EnableWindow(analyzeButton_, modificabile ? TRUE : FALSE);
    EnableWindow(settingsButton_, TRUE);
    EnableWindow(playButton_, modificabile ? TRUE : FALSE);
    EnableWindow(stopButton_, modificabile ? TRUE : FALSE);

    auto abilita = [this](int id, bool stato) {
        EnableMenuItem(mainMenu_, static_cast<UINT>(id),
                       MF_BYCOMMAND | (stato ? MF_ENABLED : MF_GRAYED));
    };

    abilita(EditorCommand::kFileSaveAs, modificabile);
    abilita(EditorCommand::kFileExport, modificabile);
    abilita(EditorCommand::kAnalyze, modificabile);
    abilita(EditorCommand::kAnalyzeSettings, TRUE);
    abilita(EditorCommand::kClearRegions, caricato);
    abilita(EditorCommand::kEditUndo, caricato && document_.canUndo());
    abilita(EditorCommand::kEditRedo, caricato && document_.canRedo());
    abilita(EditorCommand::kEditSelectAll, caricato);
    CheckMenuItem(mainMenu_, static_cast<UINT>(EditorCommand::kViewFollowPlayhead),
                  MF_BYCOMMAND | MF_ENABLED |
                  (followPlayhead_ ? MF_CHECKED : MF_UNCHECKED));
    abilita(EditorCommand::kEditDeselect, selezione);
    abilita(EditorCommand::kEditDeleteSelection, selezione);
    abilita(EditorCommand::kEditCut, selezione);
    abilita(EditorCommand::kEditSilenceSelection, selezione);
    abilita(EditorCommand::kEditCropToSelection, selezione);

    const int regione = view_.selectedRegion();
    const bool c_e_regione = caricato && regione >= 0 &&
                             regione < static_cast<int>(document_.regions().size());
    abilita(EditorCommand::kRegionDelete, c_e_regione);
    abilita(EditorCommand::kRegionToggleEnabled, c_e_regione);
    abilita(EditorCommand::kRegionSetBreath, c_e_regione);
    abilita(EditorCommand::kRegionSetClick, c_e_regione);
    abilita(EditorCommand::kRegionGainDown, c_e_regione);
    abilita(EditorCommand::kRegionGainUp, c_e_regione);
    abilita(EditorCommand::kRegionGainReset, c_e_regione);
    abilita(EditorCommand::kRegionRepairToggle, c_e_regione);
    abilita(EditorCommand::kRegionAddAtSelection, selezione);

    // Il dimezzamento agisce su TUTTI i fiati, non su una regione: serve
    // almeno un fiato perche' abbia senso.
    int fiati = 0;
    for (const EditRegion& r : document_.regions()) {
        if (r.kind == DetectionKind::Breath) ++fiati;
    }
    abilita(EditorCommand::kRegionShortenHalf, fiati > 0);
    abilita(EditorCommand::kRegionShortenThird, fiati > 0);
    abilita(EditorCommand::kRegionShortenQuarter, fiati > 0);

    abilita(EditorCommand::kTransportPlay, caricato);
    abilita(EditorCommand::kTransportPlaySelection, selezione);
    abilita(EditorCommand::kTransportStop, caricato);
    abilita(EditorCommand::kGoToStart, caricato);
    abilita(EditorCommand::kGoToEnd, caricato);
    abilita(EditorCommand::kFadeSet, caricato);
    abilita(EditorCommand::kFadeClear, caricato);

    DrawMenuBar(window_);
}

// ---------------------------------------------------------------------------
//  Dispatch dei comandi
// ---------------------------------------------------------------------------
void EditorWindow::onCommand(int commandId) {
    // I comandi che la vista manda come WM_COMMAND hanno gia' il loro id.
    switch (commandId) {
        case EditorCommand::kToolRegions:
        case EditorCommand::kToolTimeSelection:
            view_.setTimeSelectionTool(commandId == EditorCommand::kToolTimeSelection);
            refreshRegionPanel(true);
            setStatusMessage(view_.timeSelectionTool() ? "SELEZIONE TEMPORALE: trascina ovunque; bordi modificabili" : "REGIONI: Ctrl+clic multipla, Shift+clic intervallo");
            break;
        case EditorCommand::kManualAttenuate: commandManualSelection(0); break;
        case EditorCommand::kManualSilence: commandManualSelection(3); break;
        case EditorCommand::kManualCut: commandManualSelection(1); break;
        case EditorCommand::kManualHalve: commandManualSelection(2); break;
        case EditorCommand::kManualRestore: commandManualSelection(-1); break;
        case EditorCommand::kTimeSelectionBounds: commandTimeBounds(); break;
        case EditorCommand::kClicksDisable: commandClicksEnabled(false); break;
        case EditorCommand::kClicksEnable: commandClicksEnabled(true); break;
        case EditorCommand::kRegionsInTimeSelection:
            view_.selectRegionsInRange(view_.selectionStart(), view_.selectionEnd());
            refreshRegionPanel(true); updateCommandState(); break;
        case EditorCommand::kRegionFilter:
            view_.setRegionFilter(static_cast<int>(SendMessageW(regionFilter_, CB_GETCURSEL, 0, 0)));
            break;
        // --- file ---------------------------------------------------------
        case EditorCommand::kFileOpen: commandOpen(); break;
        case EditorCommand::kFileOpenInTrack: addTrackAndLoad(askOpenPath()); break;
        case EditorCommand::kFileSaveAs: commandSaveAs(); break;
        case EditorCommand::kFileExport: commandExport(); break;
        case EditorCommand::kFileExportDataset: commandExportDataset(); break;
        case EditorCommand::kFileExportOptions: commandExportOptions(); break;
        case EditorCommand::kExportFormatSame:
        case EditorCommand::kExportFormatWav:
        case EditorCommand::kExportMp3128:
        case EditorCommand::kExportMp3192:
        case EditorCommand::kExportMp3256:
        case EditorCommand::kExportMp3320:
        case EditorCommand::kExportMp3VbrQ0:
        case EditorCommand::kExportMp3VbrQ1:
        case EditorCommand::kExportMp3VbrQ2:
        case EditorCommand::kExportMp3VbrQ3:
        case EditorCommand::kExportMp3VbrQ4:
        case EditorCommand::kExportMp3VbrQ5:
        case EditorCommand::kExportMp3VbrQ6:
        case EditorCommand::kExportMp3VbrQ7:
        case EditorCommand::kExportMp3VbrQ8:
        case EditorCommand::kExportMp3VbrQ9:
            commandSetExportFormat(commandId);
            break;
        case EditorCommand::kFileSaveSession: commandSaveSession(); break;
        case EditorCommand::kFileOpenSession: commandOpenSession(); break;
        case EditorCommand::kFileBatchFolder: commandBatchFolder(); break;
        case EditorCommand::kFileBatchReviewed: commandBatchReviewed(); break;
        case EditorCommand::kFileExit: PostMessageW(window_, WM_CLOSE, 0, 0); break;

        // --- progetto multi-file -----------------------------------------
        case EditorCommand::kProjectOpenFolder: commandOpenFolder(); break;
        case EditorCommand::kProjectOpen: commandOpenProject(); break;
        case EditorCommand::kProjectSave: commandSaveProject(false); break;
        case EditorCommand::kProjectSaveAs: commandSaveProject(true); break;
        case EditorCommand::kProjectRecover: commandRecoverProject(); break;
        case EditorCommand::kProjectAnalyzeAll: commandProjectAnalyzeAll(); break;
        case EditorCommand::kProjectMarkReviewed: commandProjectMarkReviewed(); break;
        case EditorCommand::kProjectApproveAllFiles: approveAllTracks(); break;
        case EditorCommand::kProjectExportAll: commandProjectExportAll(); break;
        case EditorCommand::kProjectPreviousFile: commandProjectNextFile(-1); break;
        case EditorCommand::kProjectNextFile: commandProjectNextFile(1); break;
        case EditorCommand::kGoToSelectedRegion: commandGoToSelectedRegion(); break;
        case EditorCommand::kViewVerticalReset: commandVerticalZoomReset(); break;
        case EditorCommand::kViewFollowPlayhead: commandToggleFollowPlayhead(); break;
        case EditorCommand::kLearningClickConfirmed: commandLearningLabel(LearningLabel::ClickConfirmed); break;
        case EditorCommand::kLearningClickFalsePositive: commandLearningLabel(LearningLabel::ClickFalsePositive); break;
        case EditorCommand::kLearningMissedClick: commandLearningLabel(LearningLabel::MissedClick); break;
        case EditorCommand::kLearningMouthConfirmed: commandLearningLabel(LearningLabel::MouthClickConfirmed); break;
        case EditorCommand::kLearningVocalTransient: commandLearningLabel(LearningLabel::VocalTransient); break;
        case EditorCommand::kLearningConfirmBreath:
            commandLearningLabel(LearningLabel::BreathConfirmed); break;
        case EditorCommand::kLearningFalsePositive:
            commandLearningLabel(LearningLabel::FalsePositive); break;
        case EditorCommand::kLearningMissedBreath:
            commandLearningLabel(LearningLabel::MissedBreath); break;
        case EditorCommand::kLearningEditorialExclusion:
            commandLearningLabel(LearningLabel::EditorialExclusion); break;
        case EditorCommand::kLearningUnverified:
            commandLearningLabel(LearningLabel::Unverified); break;
        case EditorCommand::kLearningTrain: commandTrainPersonalProfile(); break;
        case EditorCommand::kLearningActivate: commandActivatePersonalProfile(!personalProfileActive_); break;
        case EditorCommand::kLearningReset: commandActivatePersonalProfile(false); break;
        case EditorCommand::kLearningStatus: commandLearningStatus(); break;
        case EditorCommand::kViewGoPlayhead: commandGoToPlayhead(); break;

        // --- tracce -------------------------------------------------------
        case EditorCommand::kTrackClose: closeActiveTrack(); break;
        case EditorCommand::kModelResetToBase: applyModelProfile("base"); break;
        case EditorCommand::kModelReload:
            loadModelProfiles();
            rebuildModelMenu();
            setStatusMessage("elenco dei profili ricaricato");
            break;

        // --- modifica -----------------------------------------------------
        case EditorCommand::kEditUndo: commandUndo(); break;
        case EditorCommand::kEditRedo: commandRedo(); break;
        case EditorCommand::kEditDeleteSelection: commandDeleteSelection(); break;
        case EditorCommand::kEditCut: commandCutSelection(); break;
        case EditorCommand::kEditSilenceSelection: commandSilenceSelection(); break;
        case EditorCommand::kEditCropToSelection: commandCropToSelection(); break;
        case EditorCommand::kEditSelectAll: commandSelectAll(); break;
        case EditorCommand::kEditDeselect:
            view_.clearSelection();
            updateStatusBar();
            updateCommandState();
            break;
        case EditorCommand::kEditInsertSilence: commandInsertSilence(); break;

        // --- rilevamento --------------------------------------------------
        case EditorCommand::kAnalyze: commandAnalyze(); break;
        case EditorCommand::kAnalyzeSettings: commandDetectionSettings(); break;
        case EditorCommand::kAnalyzeImportAi: commandImportAiRegions(); break;
        case EditorCommand::kStudioAttenuationSettings: commandAttenuationSettings(); break;
        case EditorCommand::kStudioDetectorDsp: commandDetectorMode("dsp"); break;
        case EditorCommand::kStudioDetectorAi: commandDetectorMode("ai"); break;
        case EditorCommand::kStudioDetectorFusion: commandDetectorMode("fusion"); break;
        case EditorCommand::kClickRepairAuto:
        case EditorCommand::kClickRepairLinear:
        case EditorCommand::kClickRepairPredictive: {
            auto indices=view_.selectedRegions();
            if(indices.empty()&&view_.selectedRegion()>=0)indices.push_back(view_.selectedRegion());
            document_.beginUndoGroup("Algoritmo de-click");
            for(int index:indices){
                if(index<0||index>=static_cast<int>(document_.regions().size()))continue;
                auto region=document_.regions()[index];
                if(region.kind!=DetectionKind::Click)continue;
                region.repair=true;region.editMode=0;region.manualOperation=false;
                region.repairMethod=commandId==EditorCommand::kClickRepairLinear?"linear":
                    commandId==EditorCommand::kClickRepairPredictive?"predictive":"auto";
                region.approved=region.enabled=true;region.decisionSource="user";
                document_.updateRegion(index,region);
            }
            document_.endUndoGroup();hasPreview_=false;markDocumentChanged(false);view_.refreshDocument(false);
            setStatusMessage("De-click aggiornato; anteprima/export usano lo stesso motore");break;
        }
        case EditorCommand::kPreviewResidual: {
            commandPreview();if(!hasPreview_)break;
            auto residual=studio::removedSignal(document_.audio(),preview_);
            commandStop();if(!ensurePlayerOpen())break;
            auto samples=std::make_shared<std::vector<float>>(std::move(residual.samples));
            player_.setAudio(samples,document_.frameCount());playerUsesPreview_=false;abOriginal_=true;
            player_.play(view_.hasSelection()?view_.selectionStart():view_.playhead());
            setStatusMessage("SOLO RIMOSSO: differenza allineata ai campioni originali");break;
        }
        case EditorCommand::kStudioPreview: commandPreview(); break;
        case EditorCommand::kStudioApply: commandApplyPreview(); break;
        case EditorCommand::kStudioExportPlan: commandExportPlan(); break;
        case EditorCommand::kClearRegions:
            document_.clearRegions();
            view_.setSelectedRegion(-1);
            view_.refreshDocument(false);
            setStatusMessage("regioni cancellate");
            updateTitle();
            updateCommandState();
            break;

        // --- regioni ------------------------------------------------------
        case EditorCommand::kRegionDelete: commandRegionDelete(); break;
        case EditorCommand::kRegionToggleEnabled: commandRegionToggleEnabled(); break;
        case EditorCommand::kRegionSetBreath:
            commandRegionSetKind(DetectionKind::Breath);
            break;
        case EditorCommand::kRegionSetClick:
            commandRegionSetKind(DetectionKind::Click);
            break;
        case EditorCommand::kRegionGainDown: commandRegionGain(-3.0f); break;
        case EditorCommand::kRegionGainUp: commandRegionGain(3.0f); break;
        case EditorCommand::kRegionGainReset: commandRegionGainReset(); break;
        case EditorCommand::kRegionRepairToggle: commandRegionToggleRepair(); break;
        case EditorCommand::kRegionAddAtSelection: commandRegionAddAtSelection(); break;
        case EditorCommand::kRegionConfirm: commandRegionDecision(true); break;
        case EditorCommand::kRegionReject: commandRegionDecision(false); break;
        case EditorCommand::kRegionModeAttenuate: commandRegionMode(0); break;
        case EditorCommand::kRegionModeCut: commandRegionMode(1); break;
        case EditorCommand::kRegionModeHalve: commandRegionMode(2); break;
        case EditorCommand::kRegionGlobalAttenuate: commandRegionGlobalMode(0); break;
        case EditorCommand::kRegionGlobalCut: commandRegionGlobalMode(1); break;
        case EditorCommand::kRegionGlobalHalve: commandRegionGlobalMode(2); break;
        case EditorCommand::kRegionEditBounds: commandRegionEditBounds(); break;
        case EditorCommand::kRegionKeepFraction: commandRegionKeepFraction(); break;
        case EditorCommand::kRegionKeep25: commandRegionKeepPreset(0.25); break;
        case EditorCommand::kRegionKeep50: commandRegionKeepPreset(0.50); break;
        case EditorCommand::kRegionKeep75: commandRegionKeepPreset(0.75); break;
        case EditorCommand::kRegionMargin0: commandRegionMarginPreset(0.0); break;
        case EditorCommand::kRegionMargin10: commandRegionMarginPreset(10.0); break;
        case EditorCommand::kRegionMargin20: commandRegionMarginPreset(20.0); break;
        case EditorCommand::kRegionAudition: commandAuditionRegion(); break;
        case EditorCommand::kRegionNext: commandRegionStep(1); break;
        case EditorCommand::kRegionPrevious: commandRegionStep(-1); break;
        case EditorCommand::kRegionSelectAll: commandRegionSelectAll(); break;
        case EditorCommand::kRegionClearSelection: commandRegionClearSelection(); break;
        case EditorCommand::kRegionApproveAll: commandRegionDecisionAll(true); break;
        case EditorCommand::kRegionRejectAll: commandRegionDecisionAll(false); break;
        case EditorCommand::kRegionShortenHalf: commandShortenRegions(0.5f); break;
        case EditorCommand::kRegionShortenThird: commandShortenRegions(0.34f); break;
        case EditorCommand::kRegionShortenQuarter: commandShortenRegions(0.25f); break;

        // --- vista --------------------------------------------------------
        case EditorCommand::kViewZoomIn: commandZoomIn(); break;
        case EditorCommand::kViewZoomOut: commandZoomOut(); break;
        case EditorCommand::kViewZoomFit: commandZoomFit(); break;
        case EditorCommand::kViewZoomSelection: commandZoomSelection(); break;

        // --- trasporto ----------------------------------------------------
        case EditorCommand::kTransportPlay: commandPlay(); break;
        case EditorCommand::kTransportPlaySelection: commandPlaySelection(); break;
        case EditorCommand::kTransportPlayOriginal: commandPlayMode(true); break;
        case EditorCommand::kTransportPlayProcessed: commandPlayMode(false); break;
        case EditorCommand::kTransportStop: commandStop(); break;
        case EditorCommand::kGoToStart: commandGoToStart(); break;
        case EditorCommand::kGoToEnd: commandGoToEnd(); break;

        // --- fade e aiuto -------------------------------------------------
        case EditorCommand::kFadeSet: commandFade(); break;
        case EditorCommand::kFadeClear: commandClearFade(); break;
        case EditorCommand::kHelpShortcuts: commandShowShortcuts(); break;
        case EditorCommand::kHelpAbout: commandAbout(); break;

        default:
            // Le tracce non hanno un case ciascuna: il loro identificatore e'
            // kTrackSwitchBase + indice, e la fascia e' riservata.
            if (commandId >= EditorCommand::kTrackSwitchBase &&
                commandId <= EditorCommand::kTrackSwitchMax) {
                switchToTrack(commandId - EditorCommand::kTrackSwitchBase);
            } else if (commandId >= EditorCommand::kModelSwitchBase &&
                       commandId <= EditorCommand::kModelSwitchMax) {
                const int indice = commandId - EditorCommand::kModelSwitchBase;
                const auto& elenco = models_.profiles();
                if (indice >= 0 && indice < static_cast<int>(elenco.size())) {
                    applyModelProfile(elenco[static_cast<size_t>(indice)].name);
                }
            }
            break;
    }
}

void EditorWindow::onContextMenu(int screenX, int screenY) {
    if (!regionMenu_) return;

    // Tasto destro sopra la forma d'onda: si apre l'elenco delle regioni.
    // Il menu e' lo stesso del menu principale, non una copia.
    POINT punto = {screenX, screenY};
    if (screenX == -1 && screenY == -1) {
        // Tastiera (menu contestuale): lo si apre sotto la testina.
        RECT finestra = {};
        GetWindowRect(window_, &finestra);
        punto.x = finestra.left + 40;
        punto.y = finestra.top + kToolbarHeight + 40;
    }
    if (view_.timeSelectionTool() && view_.hasSelection()) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, EditorCommand::kManualAttenuate, L"Attenua (dB e fade)...");
        AppendMenuW(menu, MF_STRING, EditorCommand::kManualSilence, L"Sostituisci con silenzio");
        AppendMenuW(menu, MF_STRING, EditorCommand::kManualCut, L"Taglia");
        AppendMenuW(menu, MF_STRING, EditorCommand::kManualHalve, L"Dimezza durata");
        AppendMenuW(menu, MF_STRING, EditorCommand::kManualRestore, L"Ripristina interventi manuali");
        AppendMenuW(menu, MF_STRING, EditorCommand::kTimeSelectionBounds, L"Modifica confini...");
        AppendMenuW(menu, MF_STRING, EditorCommand::kRegionsInTimeSelection, L"Seleziona eventi nell'intervallo");
        AppendMenuW(menu, MF_STRING, EditorCommand::kEditDeselect, L"Cancella selezione (Esc)");
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, punto.x, punto.y, 0, window_, nullptr);
        DestroyMenu(menu);
    } else TrackPopupMenu(regionMenu_, TPM_RIGHTBUTTON, punto.x, punto.y, 0, window_, nullptr);
}

// ---------------------------------------------------------------------------
//  Apertura e salvataggio
// ---------------------------------------------------------------------------
void EditorWindow::openInitialFile(const std::string& path) {
    addTrackAndLoad(path);
}

std::string EditorWindow::askOpenPath() {
    wchar_t percorso[MAX_PATH] = {};

    OPENFILENAMEW dialogo = {};
    dialogo.lStructSize = sizeof(dialogo);
    dialogo.hwndOwner = window_;
    dialogo.lpstrFilter =
        L"Audio supportato (*.wav;*.mp3;*.flac)\0*.wav;*.mp3;*.flac\0"
        L"Wave (*.wav)\0*.wav\0MP3 (*.mp3)\0*.mp3\0FLAC (*.flac)\0*.flac\0"
        L"Tutti i file (*.*)\0*.*\0";
    dialogo.lpstrFile = percorso;
    dialogo.nMaxFile = MAX_PATH;
    dialogo.lpstrTitle = L"Apri un file audio (WAV, MP3, FLAC)";
    dialogo.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&dialogo)) {
        return {};
    }
    return toUtf8(percorso);
}

std::string EditorWindow::askSavePath(const char* defaultExtension) {
    wchar_t percorso[MAX_PATH] = {};

    OPENFILENAMEW dialogo = {};
    dialogo.lStructSize = sizeof(dialogo);
    dialogo.hwndOwner = window_;
    dialogo.lpstrFilter =
        L"Audio Wave (*.wav)\0*.wav\0Tutti i file (*.*)\0*.*\0";
    dialogo.lpstrFile = percorso;
    dialogo.nMaxFile = MAX_PATH;
    dialogo.lpstrTitle = L"Salva con nome";
    dialogo.lpstrDefExt = defaultExtension ? toWide(defaultExtension).c_str() : L"wav";
    dialogo.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    // Il nome di partenza suggerisce cosa si sta salvando.
    const std::string base = defaultExtension ? defaultExtension : "wav";
    if (document_.isLoaded() && base == "wav") {
        const std::wstring nome =
            toWide(document_.displayName()) + L" - corretto.wav";
        wcsncpy_s(percorso, MAX_PATH, nome.c_str(), _TRUNCATE);
    }

    if (!GetSaveFileNameW(&dialogo)) {
        return {};
    }
    return toUtf8(percorso);
}

std::string EditorWindow::askExportPath() {
    const AudioFileInfo& source = document_.sourceInfo();
    const bool mp3 = exportOptions_.format == ExportFormat::Mp3 ||
                     (exportOptions_.format == ExportFormat::SameAsSource &&
                      source.format == AudioFileFormat::Mp3);
    const wchar_t* extension = mp3 ? L"mp3" : L"wav";
    wchar_t percorso[MAX_PATH] = {};
    OPENFILENAMEW dialogo = {};
    dialogo.lStructSize = sizeof(dialogo);
    dialogo.hwndOwner = window_;
    dialogo.lpstrFilter = mp3
        ? L"MP3 (*.mp3)\0*.mp3\0Wave (*.wav)\0*.wav\0Tutti i file (*.*)\0*.*\0"
        : L"Wave (*.wav)\0*.wav\0MP3 (*.mp3)\0*.mp3\0Tutti i file (*.*)\0*.*\0";
    dialogo.lpstrFile = percorso;
    dialogo.nMaxFile = MAX_PATH;
    dialogo.lpstrTitle = mp3 ? L"Esporta MP3 (nuova codifica lossy)" : L"Esporta WAV PCM";
    dialogo.lpstrDefExt = extension;
    dialogo.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (document_.isLoaded()) {
        std::string stem = document_.displayName();
        const size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        std::wstring suggested = toWide(stem) + L" - corretto." + extension;
        wcsncpy_s(percorso, MAX_PATH, suggested.c_str(), _TRUNCATE);
    }
    if (!GetSaveFileNameW(&dialogo)) return {};
    std::string path = toUtf8(percorso);
    std::filesystem::path out = std::filesystem::u8path(path);
    std::string current = out.extension().u8string();
    std::transform(current.begin(), current.end(), current.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string wanted = mp3 ? ".mp3" : ".wav";
    if (current != wanted) {
        out.replace_extension(wanted);
        path = out.u8string();
    }
    return path;
}

bool EditorWindow::loadFile(const std::string& path) {
    std::string error;
    if (!document_.load(path, &error)) {
        MessageBoxW(window_, toWide("Impossibile aprire il file:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        setStatusMessage("apertura non riuscita: " + error);
        return false;
    }

    player_.stop();
    statusMessage_.clear();
    analysisSummary_.clear();

    view_.setDocument(&document_);
    view_.refreshDocument(true);
    view_.zoomToFit();
    view_.setPlayhead(0);
    view_.clearSelection();

    SetWindowTextW(gainValue_, L"0.0 dB");
    SendMessageW(gainSlider_, TBM_SETPOS, TRUE, 0);

    updateTitle();
    updateStatusBar();
    updateCommandState();
    originalAudio_.reset();
    renderIfNeeded(true);
    return true;
}

void EditorWindow::adoptBuffer(AudioBuffer buffer, const std::string& name) {
    if (!document_.adopt(std::move(buffer), name)) {
        setStatusMessage("buffer non adottato");
        return;
    }

    player_.stop();
    statusMessage_.clear();
    analysisSummary_.clear();

    view_.setDocument(&document_);
    view_.refreshDocument(true);
    view_.zoomToFit();
    view_.setPlayhead(0);
    view_.clearSelection();

    updateTitle();
    updateStatusBar();
    updateCommandState();
    originalAudio_.reset();
    renderIfNeeded(true);
}

// ===========================================================================
//  Tracce multiple
// ===========================================================================
void EditorWindow::addTrackAndLoad(const std::string& path) {
    if (path.empty()) return;

    // Si carica PRIMA in un documento a parte, poi si riorganizza.
    //
    // L'ordine conta: caricando direttamente in document_ e fallendo
    // l'apertura, resterebbe rovinata la traccia di prima. Cosi' un file
    // illeggibile non tocca nulla.
    AudioDocument nuovo;
    std::string error;
    if (!nuovo.load(path, &error)) {
        MessageBoxW(window_, toWide("Impossibile aprire il file:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        setStatusMessage("apertura non riuscita: " + error);
        return;
    }

    // La traccia attuale torna nella sua cella.
    if (activeTrack_ >= 0 && activeTrack_ < static_cast<int>(tracks_.size())) {
        tracks_[activeTrack_] = std::move(document_);
    }

    // Nuova cella per la traccia che si apre, che diventa quella attiva.
    tracks_.push_back(AudioDocument{});
    trackNames_.push_back(nuovo.displayName());
    trackPaths_.push_back(path);
    trackStatus_.push_back("da_analizzare");
    trackSessionRelative_.push_back({});
    trackOutputPaths_.push_back({});
    trackAnalyzed_.push_back(false);
    trackReviewed_.push_back(false);
    trackExported_.push_back(false);
    trackErrors_.push_back({});
    activeTrack_ = static_cast<int>(tracks_.size()) - 1;
    projectDirty_ = true;
    recoveryPending_ = true;

    document_ = std::move(nuovo);
    view_.setDocument(&document_);
    refreshActiveTrack();
    rebuildTrackMenu();

    char messaggio[256];
    snprintf(messaggio, sizeof(messaggio), "aperto %s - %d tracce aperte",
             document_.displayName().c_str(),
             static_cast<int>(trackNames_.size()));
    setStatusMessage(messaggio);
}

void EditorWindow::switchToTrack(int index) {
    if (index < 0 || index >= static_cast<int>(tracks_.size())) return;
    if (index == activeTrack_) return;

    player_.stop();

    // Il documento attivo va nella sua cella, quello di destinazione prende
    // il suo posto. Sono spostamenti di vettori e stringhe: nessun campione
    // viene copiato, quindi il cambio e' immediato anche su file lunghi.
    tracks_[activeTrack_] = std::move(document_);
    document_ = std::move(tracks_[index]);
    activeTrack_ = index;

    refreshActiveTrack();
    rebuildTrackMenu();
    rebuildFileList();
}

void EditorWindow::closeActiveTrack() {
    if (activeTrack_ < 0) return;

    player_.stop();

    document_ = AudioDocument{};
    tracks_.erase(tracks_.begin() + activeTrack_);
    trackNames_.erase(trackNames_.begin() + activeTrack_);
    if (activeTrack_ < static_cast<int>(trackPaths_.size())) {
        trackPaths_.erase(trackPaths_.begin() + activeTrack_);
        trackStatus_.erase(trackStatus_.begin() + activeTrack_);
        trackSessionRelative_.erase(trackSessionRelative_.begin() + activeTrack_);
        trackOutputPaths_.erase(trackOutputPaths_.begin() + activeTrack_);
        trackAnalyzed_.erase(trackAnalyzed_.begin() + activeTrack_);
        trackReviewed_.erase(trackReviewed_.begin() + activeTrack_);
        trackExported_.erase(trackExported_.begin() + activeTrack_);
        trackErrors_.erase(trackErrors_.begin() + activeTrack_);
    }

    if (tracks_.empty()) {
        activeTrack_ = -1;
        view_.setDocument(nullptr);
    } else {
        activeTrack_ = std::min(activeTrack_, static_cast<int>(tracks_.size()) - 1);
        document_ = std::move(tracks_[activeTrack_]);
        view_.setDocument(&document_);
    }

    refreshActiveTrack();
    rebuildTrackMenu();
}

void EditorWindow::rebuildTrackMenu() {
    if (!trackMenu_) return;

    while (GetMenuItemCount(trackMenu_) > 0) {
        DeleteMenu(trackMenu_, 0, MF_BYPOSITION);
    }

    const int massimo = EditorCommand::kTrackSwitchMax -
                        EditorCommand::kTrackSwitchBase + 1;

    for (int i = 0; i < static_cast<int>(trackNames_.size()) && i < massimo; ++i) {
        wchar_t testo[260] = {};
        swprintf(testo, 260, L"%d. %s%s", i + 1,
                 toWide(trackNames_[static_cast<size_t>(i)]).c_str(),
                 i == activeTrack_ ? L"   (attiva)" : L"");
        AppendMenuW(trackMenu_,
                    MF_STRING | (i == activeTrack_ ? MF_CHECKED : MF_UNCHECKED),
                    EditorCommand::kTrackSwitchBase + i, testo);
    }

    if (!trackNames_.empty()) {
        AppendMenuW(trackMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(trackMenu_, MF_STRING, EditorCommand::kTrackClose,
                    L"Chiudi questa traccia");
    }

    DrawMenuBar(window_);
}

void EditorWindow::refreshActiveTrack() {
    if (!document_.isLoaded()) {
        view_.clearSelection();
        updateTitle();
        updateStatusBar();
        updateCommandState();
        return;
    }

    view_.setDocument(&document_);
    view_.refreshDocument(true);
    view_.zoomToFit();
    view_.setPlayhead(0);
    view_.clearSelection();

    // Il guadagno generale appartiene al documento, non alla finestra: si
    // riporta il cursore sul valore della traccia appena aperta.
    const float db = document_.masterGainDb();
    wchar_t etichetta[32] = {};
    swprintf(etichetta, 32, L"%.1f dB", static_cast<double>(db));
    SetWindowTextW(gainValue_, etichetta);
    SendMessageW(gainSlider_, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(-db * 10.0f));

    updateTitle();
    updateStatusBar();
    updateCommandState();
    rebuildFileList();
    refreshRegionPanel(true);
    renderIfNeeded(true);
}

// ===========================================================================
//  Profili di modello
// ===========================================================================
void EditorWindow::loadModelProfiles() {
    std::string avviso;
    const std::string radice = resolveModelsRoot();

    if (radice.empty()) {
        // Senza cartella si resta sul profilo incorporato: il programma
        // funziona lo stesso, con i valori predefiniti.
        activeProfile_ = "base";
        return;
    }

    models_.scan(radice, &avviso);
    activeProfile_ = "base";

    // Si parte dal profilo base: un profilo addestrato scelto in una sessione
    // precedente non deve imporsi da solo, perche' potrebbe essere stato
    // pensato per un altro materiale.
    applyProfile(models_.base(), detectorSettings_);
}

void EditorWindow::rebuildModelMenu() {
    if (!modelMenu_) return;

    while (GetMenuItemCount(modelMenu_) > 0) {
        DeleteMenu(modelMenu_, 0, MF_BYPOSITION);
    }

    const auto& elenco = models_.profiles();
    const int massimo = EditorCommand::kModelSwitchMax -
                        EditorCommand::kModelSwitchBase + 1;

    for (int i = 0; i < static_cast<int>(elenco.size()) && i < massimo; ++i) {
        const ModelProfile& profilo = elenco[static_cast<size_t>(i)];

        // Il materiale si mostra accanto al nome: aiuta a scegliere quello
        // giusto per un file gia' trattato invece di indovinare.
        wchar_t testo[300] = {};
        swprintf(testo, 300, L"%s   [%s]", toWide(profilo.name).c_str(),
                 toWide(profilo.materialType).c_str());

        AppendMenuW(modelMenu_,
                    MF_STRING | (profilo.name == activeProfile_ ? MF_CHECKED
                                                                : MF_UNCHECKED),
                    EditorCommand::kModelSwitchBase + i, testo);
    }

    AppendMenuW(modelMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(modelMenu_, MF_STRING, EditorCommand::kModelResetToBase,
                L"Ripristina il profilo base");
    AppendMenuW(modelMenu_, MF_STRING, EditorCommand::kModelReload,
                L"Ricarica l'elenco dei profili");

    DrawMenuBar(window_);
}

void EditorWindow::applyModelProfile(const std::string& name) {
    const ModelProfile* profilo = models_.find(name);
    if (!profilo) {
        // Ripiego sul base invece di rifiutare: se il profilo e' sparito
        // dalla cartella, meglio i valori predefiniti che niente.
        applyProfile(models_.base(), detectorSettings_);
        activeProfile_ = "base";
        rebuildModelMenu();
        updateStatusBar();
        return;
    }

    applyProfile(*profilo, detectorSettings_);
    activeProfile_ = profilo->name;

    rebuildModelMenu();
    updateStatusBar();

    const char* modello = profilo->hasNet()
        ? ", rete dal profilo"
        : (profilo->hasBreathModel() ? ", modello lineare" : "");

    char messaggio[400];
    snprintf(messaggio, sizeof(messaggio),
             "profilo '%s' (%s): fiati %.3f, click %.2f%s",
             profilo->name.c_str(), profilo->materialType.c_str(),
             static_cast<double>(profilo->breathThreshold),
             static_cast<double>(profilo->clickThreshold), modello);
    setStatusMessage(messaggio);
}

void EditorWindow::commandOpen() {
    addTrackAndLoad(askOpenPath());
}

void EditorWindow::commandSaveAs() {
    if (!document_.isLoaded()) return;

    const std::string percorso = askExportPath();
    if (percorso.empty()) return;

    std::string error;
    const bool mp3 = exportOptions_.format == ExportFormat::Mp3 ||
                     (exportOptions_.format == ExportFormat::SameAsSource &&
                      document_.sourceInfo().format == AudioFileFormat::Mp3);
    if (mp3) {
        if (!exportActiveTrackTo(percorso, &error)) {
            MessageBoxW(window_, toWide("Salvataggio non riuscito:\n\n" + error).c_str(),
                        L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
            return;
        }
    } else if (!document_.save(percorso, WavFormat::Pcm24, editSettings_, &error)) {
        MessageBoxW(window_, toWide("Salvataggio non riuscito:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }

    document_.markSaved();
    updateTitle();
    setStatusMessage("salvato in " + percorso);
}

void EditorWindow::commandExport() {
    if (!document_.isLoaded()) return;
    if (studioMode_) {
        commandApplyPreview();
        return;
    }

    const std::string percorso = askSavePath("wav");
    if (percorso.empty()) return;

    std::string error;
    if (!document_.save(percorso, WavFormat::Pcm24, editSettings_, &error)) {
        MessageBoxW(window_, toWide("Esportazione non riuscita:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }

    // L'esportazione produce un file NUOVO: il documento resta com'era e
    // l'audio originale non viene toccato, per scelta di progetto.
    studio::Report report;
    report.analysis.inputPath = document_.sourcePath();
    report.analysis.sampleRate = document_.sampleRate();
    report.analysis.channels = document_.channels();
    report.analysis.frames = document_.frameCount();
    report.analysis.durationSeconds = document_.audio().durationSeconds();
    report.analysis.detector = studioDetector_;
    report.outputPath = percorso;
    report.settings.reductionDb = editSettings_.breathReductionDb;
    report.settings.fadeMs = editFadeMs_;
    report.settings.detector = studioDetector_;
    const AudioBuffer rendered = document_.render(editSettings_);
    for (const EditRegion& region : document_.regions()) {
        if (!region.enabled) continue;
        const int64_t length = region.endSample - region.startSample;
        if (length <= 0) continue;
        const int64_t margin = std::max<int64_t>(1, length / 4);
        studio::EventMetric metric;
        metric.startSample = region.startSample;
        metric.endSample = region.endSample;
        metric.rmsBeforeDb = studio::rmsDb(document_.audio(), region.startSample + margin,
                                           region.endSample - margin, document_.channels());
        metric.rmsAfterDb = studio::rmsDb(rendered, region.startSample + margin,
                                          region.endSample - margin, document_.channels());
        metric.deltaDb = metric.rmsAfterDb - metric.rmsBeforeDb;
        report.metrics.push_back(metric);
        ++report.processedEvents;
    }
    std::string reportError;
    studio::writeReportJson(percorso + ".report.json", report, &reportError);
    studio::writeReportText(percorso + ".report.txt", report, &reportError);
    setStatusMessage("audio corretto esportato in " + percorso + " (report accanto)");
}

// ===========================================================================
//  Sessione e batch di NL Breath Click Studio
// ===========================================================================
void EditorWindow::commandSaveSession() {
    if (!document_.isLoaded()) {
        setStatusMessage("aprire prima un file audio");
        return;
    }
    std::string path;
    if (!chooseFilePath(window_, true,
                        L"Sessione NL Breath Click Studio (*.bcnstudio.json)\0*.bcnstudio.json\0JSON (*.json)\0*.json\0",
                        L"bcnstudio.json", path)) {
        return;
    }
    std::string error;
    if (!studio::saveSession(path, makeSessionFor(document_), &error)) {
        MessageBoxW(window_, toWide("Salvataggio sessione non riuscito:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    if (activeTrack_ >= 0 && activeTrack_ < static_cast<int>(trackSessionRelative_.size())) {
        trackSessionRelative_[static_cast<size_t>(activeTrack_)] = path;
    }
    setStatusMessage("sessione salvata in " + path);
}

void EditorWindow::commandOpenSession() {
    std::string path;
    if (!chooseFilePath(window_, false,
                        L"Sessione NL Breath Click Studio (*.bcnstudio.json)\0*.bcnstudio.json\0JSON (*.json)\0*.json\0",
                        L"bcnstudio.json", path)) {
        return;
    }
    openSessionFile(path);
}

bool EditorWindow::openSessionFile(const std::string& path) {
    studio::SessionData session;
    std::string error;
    if (!studio::loadSession(path, &session, &error)) {
        MessageBoxW(window_, toWide("Sessione non leggibile:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return false;
    }
    AudioFileInfo sourceInfo;
    if (!probeAudio(session.sourcePath, sourceInfo, &error)) {
        MessageBoxW(window_, toWide("Audio della sessione non leggibile: " + error).c_str(),
                    L"Sessione", MB_OK | MB_ICONERROR);
        return false;
    }
    for (const auto& region : session.regions) {
        if (region.endSample > sourceInfo.frames) {
            MessageBoxW(window_, L"La sessione contiene regioni fuori dall'audio. Il lavoro corrente resta aperto.",
                        L"Sessione", MB_OK | MB_ICONERROR);
            return false;
        }
    }
    if (!document_.isLoaded() || document_.sourcePath() != session.sourcePath) {
        if (!loadFile(session.sourcePath)) return false;
    }
    // Aprire una sessione da zero deve creare anche la riga nel progetto:
    // altrimenti il pannello resta vuoto e l'esportazione di progetto non
    // trova alcuna traccia.
    if (activeTrack_ < 0) {
        tracks_.push_back(AudioDocument{});
        trackNames_.push_back(document_.displayName());
        trackPaths_.push_back(document_.sourcePath());
        trackStatus_.push_back("analizzato");
        trackSessionRelative_.push_back({});
        trackOutputPaths_.push_back({});
        trackAnalyzed_.push_back(true);
        trackReviewed_.push_back(false);
        trackExported_.push_back(false);
        trackErrors_.push_back({});
        activeTrack_ = 0;
    }
    applySessionTo(&document_, session);
    if (session.detectorSettings) detectorSettings_ = *session.detectorSettings;
    editSettings_.breathReductionDb = session.reductionDb;
    editFadeMs_ = session.fadeMs;
    editCrossfadeMs_ = session.crossfadeMs;
    editSettings_.attackMs = static_cast<float>(session.fadeMs);
    editSettings_.releaseMs = static_cast<float>(session.fadeMs);
    if (!session.detector.empty()) studioDetector_ = session.detector;
    aiThreshold_ = session.aiThreshold;
    editSettings_.prePadMs = 0.0f;
    editSettings_.postPadMs = 0.0f;
    if (activeTrack_ >= 0) {
        trackAnalyzed_[static_cast<size_t>(activeTrack_)] = true;
        trackSessionRelative_[static_cast<size_t>(activeTrack_)] = path;
        trackErrors_[static_cast<size_t>(activeTrack_)].clear();
        setTrackStatus(activeTrack_);
    }
    view_.setSelectedRegion(-1);
    view_.refreshDocument(false);
    markDocumentChanged(false);
    refreshTrackStatus();
    setStatusMessage("sessione aperta: " + path);
    return true;
}

void EditorWindow::commandBatchFolder() {
    std::string directory;
    if (!chooseDirectory(window_, L"Cartella con registrazioni WAV/MP3/FLAC da elaborare", directory)) {
        return;
    }
    const auto settings = makeStudioSettings();
    runBatchWindow(window_, directory, settings);
    setStatusMessage("batch completato; cartella breathclick_out nella cartella scelta");
}

void EditorWindow::commandBatchReviewed() {
    std::string inputDirectory;
    std::string plansDirectory;
    if (!chooseDirectory(window_, L"Cartella con registrazioni WAV/MP3/FLAC", inputDirectory)) return;
    if (!chooseDirectory(window_, L"Cartella con i piani .plan.json revisionati", plansDirectory)) return;
    const std::string outputDirectory = (std::filesystem::u8path(inputDirectory) /
                                         "breathclick_reviewed").u8string();
    studio::BatchReviewReport report;
    std::string error;
    if (!studio::processFolderReviewed(inputDirectory, outputDirectory, plansDirectory,
                                      &report, &error)) {
        MessageBoxW(window_, toWide("Batch revisionato non riuscito:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    std::string message = "Batch revisionato: " + std::to_string(report.applied) +
        " applicati, " + std::to_string(report.skipped) + " da rivedere/saltati.\n\n" +
        "Uscita: " + outputDirectory;
    MessageBoxW(window_, toWide(message).c_str(), L"NL Breath Click Studio", MB_OK);
    setStatusMessage("batch revisionato: " + std::to_string(report.applied) + " applicati");
}

void EditorWindow::commandAttenuationSettings() {
    float reduction = editSettings_.breathReductionDb;
    double fade = editFadeMs_;
    double crossfade = editCrossfadeMs_;
    if (!showAttenuationDialog(window_, reduction, fade, &crossfade)) return;

    editSettings_.breathReductionDb = reduction;
    editSettings_.attackMs = static_cast<float>(fade);
    editSettings_.releaseMs = static_cast<float>(fade);
    editSettings_.prePadMs = 0.0f;
    editSettings_.postPadMs = 0.0f;
    editFadeMs_ = fade;
    editCrossfadeMs_ = crossfade;

    document_.beginUndoGroup("Attenuazione globale");
    for (size_t i = 0; i < document_.regions().size(); ++i) {
        EditRegion region = document_.regions()[i];
        if (region.manualOperation || region.kind != DetectionKind::Breath) continue;
        region.reductionDb = reduction;
        region.fadeMs = fade;
        document_.updateRegion(static_cast<int>(i), region);
    }
    document_.endUndoGroup();
    markDocumentChanged(false);
    char message[128] = {};
    snprintf(message, sizeof(message), "attenuazione %.1f dB, fade %.0f ms", reduction, fade);
    setStatusMessage(message);
}

// ===========================================================================
//  Revisione, anteprima e applicazione del piano condiviso
// ===========================================================================
bool EditorWindow::buildEditPlan(studio::EditPlan* plan, std::string* error) {
    if (!plan) return false;
    if (!document_.isLoaded() || document_.sourcePath().empty()) {
        if (error) *error = "aprire un WAV da disco";
        return false;
    }
    plan->sourcePath = document_.sourcePath();
    plan->sampleRate = document_.sampleRate();
    plan->channels = document_.channels();
    plan->masterGainDb = document_.masterGainDb();
    plan->fade = document_.fade();
    plan->settings = makeStudioSettings();
    plan->analysis = document_.analysisProvenance();
    plan->settings.reductionDb = editSettings_.breathReductionDb;
    plan->settings.fadeMs = editFadeMs_;
    plan->settings.crossfadeMs = editCrossfadeMs_;
    plan->markerScope = "voice";
    plan->settings.detector = studioDetector_;
    if (!plan->analysis.detector.empty()) plan->settings.detector = plan->analysis.detector;
    plan->events.clear();
    plan->markers.clear();
    for (const EditRegion& region : document_.regions()) {
        // L'inclusione e' il flag di piano; non richiede una conferma
        // individuale. Un evento escluso resta nel documento ma non entra qui.
        if (!region.enabled || !region.approved) continue;
        studio::Event event = studio::eventFromRegion(region);
        plan->events.push_back(event);
    }
    return true;
}

void EditorWindow::commandRegionMode(int mode) {
    std::vector<int> indices = view_.selectedRegions();
    if (indices.empty() && view_.selectedRegion() >= 0) indices.push_back(view_.selectedRegion());
    if (indices.empty()) {
        setStatusMessage("selezionare almeno un evento");
        return;
    }
    int changed = 0;
    int pending = 0;
    document_.beginUndoGroup("Modalita regioni");
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(document_.regions().size())) continue;
        EditRegion region = document_.regions()[static_cast<size_t>(index)];
        // La modalita' si puo' assegnare anche a un evento escluso: viene
        // conservata e sara' applicata quando l'evento verra' reincluso.
        if(region.kind==DetectionKind::Click && mode!=0)continue;
        if(region.kind==DetectionKind::Click)region.repair=false;
        region.editMode = std::max(0, std::min(2, mode));
        document_.updateRegion(index, region);
        if (!region.enabled || !region.approved) ++pending;
        ++changed;
    }
    document_.endUndoGroup();
    hasPreview_ = false;
    view_.refreshDocument(false);
    refreshRegionPanel();
    const char* names[] = {"Attenua", "Taglia", "Dimezza"};
    char message[160] = {};
    std::snprintf(message, sizeof(message),
                  "modalita' %s su %d eventi%s",
                  names[std::max(0, std::min(2, mode))], changed,
                  pending > 0 ? " (esclusi dal piano; modalita conservata)" : "");
    setStatusMessage(message);
    markDocumentChanged(false);
}

void EditorWindow::commandRegionGlobalMode(int mode) {
    if (!document_.isLoaded()) return;
    defaultEditMode_ = std::max(0, std::min(2, mode));
    int changed = 0;
    document_.beginUndoGroup("Modalita globale");
    for (size_t i = 0; i < document_.regions().size(); ++i) {
        EditRegion region = document_.regions()[i];
        if (!region.enabled || !region.approved) continue; // le eccezioni restano
        if(region.kind==DetectionKind::Click && defaultEditMode_!=0)continue;
        if(region.kind==DetectionKind::Click)region.repair=false;
        region.editMode = defaultEditMode_;
        document_.updateRegion(static_cast<int>(i), region);
        ++changed;
    }
    document_.endUndoGroup();
    hasPreview_ = false;
    view_.refreshDocument(false);
    refreshRegionPanel();
    const char* names[] = {"Attenua", "Taglia", "Dimezza"};
    char message[128] = {};
    std::snprintf(message, sizeof(message),
                  "modalita' globale %s applicata a %d eventi inclusi",
                  names[defaultEditMode_], changed);
    setStatusMessage(message);
    markDocumentChanged(false);
}

void EditorWindow::commandRegionKeepFraction() {
    std::vector<int> indices = view_.selectedRegions();
    if (indices.empty() && view_.selectedRegion() >= 0) indices.push_back(view_.selectedRegion());
    if (indices.empty()) {
        setStatusMessage("selezionare almeno un evento");
        return;
    }
    int anchor = indices.front();
    if (anchor < 0 || anchor >= static_cast<int>(document_.regions().size())) return;
    double fraction = document_.regions()[static_cast<size_t>(anchor)].keepFraction;
    if (!showKeepFractionDialog(window_, fraction)) return;
    document_.beginUndoGroup("Frazione conservata");
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(document_.regions().size())) continue;
        EditRegion region = document_.regions()[static_cast<size_t>(index)];
        region.keepFraction = fraction;
        document_.updateRegion(index, region);
    }
    document_.endUndoGroup();
    hasPreview_ = false;
    view_.refreshDocument(false);
    char message[96] = {};
    snprintf(message, sizeof(message), "Dimezza: conserva %.0f%%", fraction * 100.0);
    setStatusMessage(message);
    markDocumentChanged(false);
}

void EditorWindow::commandRegionKeepPreset(double fraction) {
    std::vector<int> indices = view_.selectedRegions();
    if (indices.empty() && view_.selectedRegion() >= 0) indices.push_back(view_.selectedRegion());
    if (indices.empty()) {
        setStatusMessage("selezionare almeno un evento");
        return;
    }
    document_.beginUndoGroup("Frazione conservata");
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(document_.regions().size())) continue;
        EditRegion region = document_.regions()[static_cast<size_t>(index)];
        region.keepFraction = fraction;
        document_.updateRegion(index, region);
    }
    document_.endUndoGroup();
    hasPreview_ = false;
    view_.refreshDocument(false);
    char message[96] = {};
    std::snprintf(message, sizeof(message), "Dimezza: conserva %.0f%%", fraction * 100.0);
    setStatusMessage(message);
    markDocumentChanged(false);
}

void EditorWindow::commandRegionMarginPreset(double marginMs) {
    std::vector<int> indices = view_.selectedRegions();
    if (indices.empty() && view_.selectedRegion() >= 0) indices.push_back(view_.selectedRegion());
    if (indices.empty()) {
        setStatusMessage("selezionare almeno un evento");
        return;
    }
    document_.beginUndoGroup("Margine regioni");
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(document_.regions().size())) continue;
        EditRegion region = document_.regions()[static_cast<size_t>(index)];
        region.safetyMarginMs = marginMs;
        document_.updateRegion(index, region);
    }
    document_.endUndoGroup();
    hasPreview_ = false;
    view_.refreshDocument(false);
    char message[96] = {};
    std::snprintf(message, sizeof(message), "margine sicurezza: %.0f ms", marginMs);
    setStatusMessage(message);
    markDocumentChanged(false);
}

void EditorWindow::commandAuditionRegion() {
    const int index = view_.selectedRegion();
    if (index < 0 || index >= static_cast<int>(document_.regions().size())) {
        setStatusMessage("selezionare un respiro");
        return;
    }
    const EditRegion& region = document_.regions()[static_cast<size_t>(index)];
    view_.setSelection(region.startSample, region.endSample);
    abOriginal_ = true;
    commandPlayMode(true);
    setStatusMessage("ascolto dell'evento selezionato (originale)");
}

void EditorWindow::commandRegionStep(int delta) {
    const int count = static_cast<int>(document_.regions().size());
    if (count == 0) {
        setStatusMessage("nessun evento");
        return;
    }
    int index = view_.selectedRegion();
    index = index < 0 ? (delta > 0 ? 0 : count - 1) : (index + delta + count) % count;
    int attempts = 0;
    while (!view_.regionVisible(index) && attempts++ < count) index = (index + delta + count) % count;
    if (!view_.regionVisible(index)) { setStatusMessage("nessun evento nel filtro corrente"); return; }
    view_.setSelectedRegion(index);
    const EditRegion& region = document_.regions()[static_cast<size_t>(index)];
    view_.setPlayhead(region.startSample);
    updateStatusBar();
    updateCommandState();
}

void EditorWindow::commandPreview() {
    studio::EditPlan plan;
    std::string error;
    if (!buildEditPlan(&plan, &error)) {
        MessageBoxW(window_, toWide("Anteprima non disponibile:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!studio::applyEditPlan(document_.audio(), plan, &preview_, &error)) {
        MessageBoxW(window_, toWide("Anteprima non applicabile:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    auto copy = std::make_shared<std::vector<float>>(preview_.audio.samples);
    previewAudio_ = copy;
    hasPreview_ = true;
    engineApplied_ = preview_.appliedEvents; engineRejected_ = preview_.rejectedEvents;
    refreshRegionPanel(true);
    player_.stop();
    char message[256] = {};
    snprintf(message, sizeof(message),
             "anteprima pronta: %.3f -> %.3f s (%.3f s rimossi, %d interventi, %d click conservati per rischio vocale)",
             document_.audio().durationSeconds(), preview_.audio.durationSeconds(),
             static_cast<double>(preview_.removedSamples) / std::max(1, document_.sampleRate()),
             preview_.appliedEvents,preview_.declinedEvents);
    setStatusMessage(message);
    updateStatusBar();
}

void EditorWindow::commandExportPlan() {
    studio::EditPlan plan;
    std::string error;
    if (!buildEditPlan(&plan, &error)) {
        MessageBoxW(window_, toWide("Piano non esportabile:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string path;
    if (!chooseFilePath(window_, true,
            L"Piano NL Breath Click Studio (*.json)\0*.json\0Tutti i file (*.*)\0*.*\0",
            L"Esporta piano versionato", path)) {
        return;
    }
    if (!studio::writeEditPlanJson(path, plan, &error)) {
        MessageBoxW(window_, toWide("Piano non scritto:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    setStatusMessage("piano versionato esportato in " + path);
}

void EditorWindow::commandExportOptions() {
    const char* label = "stesso formato dell'originale";
    switch (exportOptions_.format) {
        case ExportFormat::Wav: label = "WAV PCM"; break;
        case ExportFormat::Mp3: label = exportOptions_.mp3.vbr ? "MP3 VBR" : "MP3 CBR"; break;
        default: break;
    }
    setStatusMessage(std::string("formato esportazione: ") + label);
}

void EditorWindow::commandSetExportFormat(int commandId) {
    exportOptions_.format = ExportFormat::SameAsSource;
    exportOptions_.mp3.vbr = false;
    exportOptions_.mp3.bitrateKbps = 192;
    exportOptions_.mp3.vbrQuality = 2;
    std::string label = "stesso formato dell'originale";
    switch (commandId) {
        case EditorCommand::kExportFormatWav:
            exportOptions_.format = ExportFormat::Wav;
            label = "WAV PCM 24 bit";
            break;
        case EditorCommand::kExportMp3128:
        case EditorCommand::kExportMp3192:
        case EditorCommand::kExportMp3256:
        case EditorCommand::kExportMp3320:
            exportOptions_.format = ExportFormat::Mp3;
            exportOptions_.mp3.vbr = false;
            exportOptions_.mp3.bitrateKbps = commandId == EditorCommand::kExportMp3128 ? 128
                : commandId == EditorCommand::kExportMp3192 ? 192
                : commandId == EditorCommand::kExportMp3256 ? 256 : 320;
            label = "MP3 CBR " + std::to_string(exportOptions_.mp3.bitrateKbps) + " kbps";
            break;
        default:
            if (commandId >= EditorCommand::kExportMp3VbrQ0 &&
                commandId <= EditorCommand::kExportMp3VbrQ9) {
                exportOptions_.format = ExportFormat::Mp3;
                exportOptions_.mp3.vbr = true;
                exportOptions_.mp3.vbrQuality =
                    commandId - EditorCommand::kExportMp3VbrQ0;
                label = "MP3 VBR Q" + std::to_string(exportOptions_.mp3.vbrQuality);
            }
            break;
    }
    setStatusMessage("formato esportazione: " + label +
                     (exportOptions_.format == ExportFormat::SameAsSource ? "" :
                      " (MP3: nuova codifica lossy, non lossless)"));
}

void EditorWindow::commandApplyPreview() {
    if (!hasPreview_) {
        commandPreview();
        if (!hasPreview_) return;
    }
    const std::string path = askExportPath();
    if (path.empty()) return;
    std::error_code pathError;
    const auto sourceAbs = std::filesystem::absolute(
        std::filesystem::u8path(document_.sourcePath()), pathError).lexically_normal();
    const auto outputAbs = std::filesystem::absolute(
        std::filesystem::u8path(path), pathError).lexically_normal();
    if (!pathError && sourceAbs == outputAbs) {
        MessageBoxW(window_, L"Il file di uscita non puo' essere l'originale.",
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    const std::wstring summary = toWide(globalEditSummary(false));
    if (MessageBoxW(window_, summary.c_str(), L"NL Breath Click Studio - riepilogo",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    std::string error;
    if (!exportActiveTrackTo(path, &error)) {
        MessageBoxW(window_, toWide("Esportazione non riuscita:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        return;
    }
    setStatusMessage("piano applicato ed esportato in " + path);
}

void EditorWindow::commandDetectionSettings() {
    DetectorSettings copia = detectorSettings_;
    DetectionPreviewContext preview;
    preview.editSettings = editSettings_;
    if (document_.isLoaded()) {
        preview.document = &document_;
        const int64_t focus = view_.playhead();
        const int64_t visible = view_.visibleFrameCount();
        preview.window = choosePreviewWindow(document_, focus, visible);
        for (auto detection : detectionsFromDocument(document_)) {
            if (detection.endSample <= preview.window.startFrame ||
                detection.startSample >= preview.window.endFrame) continue;
            detection.startSample = std::max(detection.startSample, preview.window.startFrame);
            detection.endSample = std::min(detection.endSample, preview.window.endFrame);
            preview.before.push_back(detection);
        }
        const AudioBuffer& audio = document_.audio();
        const int channels = std::max(1, audio.channels);
        const int64_t start = std::max<int64_t>(0, preview.window.startFrame);
        const int64_t end = std::min<int64_t>(audio.frameCount(), preview.window.endFrame);
        if (end > start) {
            const size_t offset = static_cast<size_t>(start * channels);
            const size_t count = static_cast<size_t>((end - start) * channels);
            if (offset + count <= audio.samples.size()) {
                auto samples = std::make_shared<std::vector<float>>(
                    audio.samples.begin() + static_cast<std::ptrdiff_t>(offset),
                    audio.samples.begin() + static_cast<std::ptrdiff_t>(offset + count));
                preview.auditionSamples = samples;
                preview.sampleRate = audio.sampleRate;
                preview.channels = channels;
            }
        }
    }
    if (showSettingsDialog(window_, copia, preview.document ? &preview : nullptr)) {
        detectorSettings_ = copia;
        markDocumentChanged(false);
        setStatusMessage("impostazioni di rilevamento confermate e salvate");
    }
}

void EditorWindow::commandAnalyze() {
    std::string error;
    if (!analyzeActiveTrack(true, &error)) {
        if (!error.empty()) setStatusMessage("analisi non riuscita: " + error);
    }
}

void EditorWindow::commandDetectorMode(const std::string& mode) {
    studioDetector_ = mode;
    std::string label = "DSP";
    if (mode == "ai") label = "Respiro-en";
    else if (mode == "fusion") label = "DSP + Respiro-en";
    setStatusMessage("metodo di analisi: " + label);
}


// ---------------------------------------------------------------------------
//  Importa le regioni dall'analisi AI
//
//  Il rilevamento con la rete neurale gira in Python: produce un file JSON
//  con le posizioni di fiati e click. Qui si legge e si trasforma in regioni
//  modificabili, esattamente come se fossero state trovate dal DSP.
//
//  La differenza rispetto ad "Analizza" e' che quelle regioni sono state
//  trovate da un modello addestrato, non da soglie: l'utente puo' comunque
//  correggerle a mano, e ogni correzione e' registrata per il riaddestramento.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  Esporta le correzioni come dati di addestramento
//
//  Le regioni che vedi sono il risultato finale: quelle tenute sono esempi
//  positivi, quelle cancellate esempi negativi. La differenza fra cio' che il
//  rilevatore ha proposto e cio' che hai lasciato e' esattamente cio' che il
//  modello deve imparare.
// ---------------------------------------------------------------------------
void EditorWindow::commandExportDataset() {
    if (!document_.isLoaded()) {
        setStatusMessage("aprire prima un file audio");
        return;
    }

    if (document_.corrections().empty()) {
        MessageBoxW(window_,
            L"Nessuna correzione registrata.\n\n"
            L"Si registrano quando cancelli una regione o ne cambi il tipo: "
            L"sono il segnale che il rilevatore ha sbagliato.",
            L"NL Breath Click Studio", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Accanto al file audio: e' il posto in cui addestra_estrai_dataset.py
    // cerca le correzioni. Niente finestra di salvataggio a ogni correzione:
    // il gesto deve costare zero, altrimenti non lo si fa.
    std::string percorso;
    if (!document_.sourcePath().empty()) {
        percorso = document_.sourcePath() + ".dataset.json";
    } else {
        percorso = askSavePath("json");
        if (percorso.empty()) return;
    }

    std::string error;
    if (!document_.exportCorrections(percorso, &error)) {
        MessageBoxW(window_, toWide("Esportazione non riuscita:\n\n" + error).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        setStatusMessage("esportazione non riuscita: " + error);
        return;
    }

    char messaggio[512];
    snprintf(messaggio, sizeof(messaggio),
             "esportati %d esempi verificati per l'apprendimento in %s",
             static_cast<int>(collectLearningExamples(document_).size()), percorso.c_str());
    setStatusMessage(messaggio);
}

void EditorWindow::commandImportAiRegions() {
    if (!document_.isLoaded()) {
        setStatusMessage("aprire prima un file audio");
        return;
    }

    wchar_t percorso[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window_;
    ofn.lpstrFilter = L"Regioni AI (*.json)\0*.json\0Tutti i file (*.*)\0*.*\0";
    ofn.lpstrFile = percorso;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Importa le regioni trovate dall'analisi AI";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return;

    const std::string utf8 = toUtf8(percorso);

    AiRegions importate;
    std::string errore;
    if (!readAiRegions(utf8, &importate, &errore)) {
        MessageBoxW(window_, toWide("Regioni non leggibili:\n\n" + errore).c_str(),
                    L"NL Breath Click Studio", MB_OK | MB_ICONERROR);
        setStatusMessage("importazione non riuscita: " + errore);
        return;
    }

    const double sr = document_.sampleRate();
    const int64_t campioni = document_.frameCount();

    std::vector<Detection> rilevamenti;
    rilevamenti.reserve(importate.regions.size());

    for (const AiRegion& r : importate.regions) {
        Detection d;
        d.kind = (r.kind == AiRegion::Kind::Click) ? DetectionKind::Click
                                                   : DetectionKind::Breath;

        // I secondi del file AI diventano campioni del documento. Se la
        // frequenza non coincide (file ricampionato) le posizioni sarebbero
        // sbagliate: si segnala invece di applicare correzioni a caso.
        if (importate.sampleRate > 0 &&
            std::abs(importate.sampleRate - static_cast<int>(sr)) > 1) {
            MessageBoxW(window_,
                        toWide("Le regioni sono state calcolate a " +
                               std::to_string(importate.sampleRate) +
                               " Hz, ma il file aperto e' a " +
                               std::to_string(static_cast<int>(sr)) +
                               " Hz.\n\nRiaprire il file originale.").c_str(),
                        L"NL Breath Click Studio", MB_OK | MB_ICONWARNING);
            setStatusMessage("frequenza diversa: regioni non importate");
            return;
        }

        d.startSample = static_cast<int64_t>(std::llround(r.startSeconds * sr));
        d.endSample = static_cast<int64_t>(std::llround(r.endSeconds * sr));
        d.startSample = std::max<int64_t>(0, std::min(d.startSample, campioni));
        d.endSample = std::max<int64_t>(d.startSample,
                                        std::min(d.endSample, campioni));
        if (d.endSample <= d.startSample) continue;

        d.peakScore = r.confidence;
        d.meanScore = r.confidence;
        rilevamenti.push_back(d);
    }

    if (rilevamenti.empty()) {
        setStatusMessage("il file non contiene regioni utilizzabili");
        return;
    }

    player_.stop();
    document_.setRegionsFromDetections(rilevamenti, editSettings_);
    view_.setSelectedRegion(-1);
    view_.refreshDocument(false);
    markDocumentChanged(false);

    char messaggio[256];
    snprintf(messaggio, sizeof(messaggio),
             "importate %d regioni (%d fiati, %d click)",
             static_cast<int>(rilevamenti.size()),
             importate.countOf(AiRegion::Kind::Breath),
             importate.countOf(AiRegion::Kind::Click));
    statusMessage_ = messaggio;
    analysisSummary_ = messaggio;
    updateStatusBar();
}

void EditorWindow::commandApplyRegionsToDocument(bool createRegions) {
    if (!document_.isLoaded()) return;

    if (!createRegions) {
        document_.clearRegions();
    } else {
        commandAnalyze();
        return;
    }

    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage("regioni aggiornate");
}

// ===========================================================================
//  Comandi: modifica
// ===========================================================================
void EditorWindow::commandUndo() {
    player_.stop();
    if (!document_.undo()) return;

    view_.clearSelection();
    markDocumentChanged(true);
    setStatusMessage("annullato");
}

void EditorWindow::commandRedo() {
    player_.stop();
    if (!document_.redo()) return;

    view_.clearSelection();
    markDocumentChanged(true);
    setStatusMessage("ripristinato");
}

void EditorWindow::commandDeleteSelection() {
    if (studioMode_ && view_.timeSelectionTool() && view_.hasSelection()) { commandManualSelection(1); return; }
    // Canc su un evento lo ESCLUDE dal piano, senza toglierlo dal documento:
    // deve restare visibile e riattivabile. La rimozione definitiva e' nel
    // menu contestuale "Elimina definitivamente".
    if (view_.selectedRegion() >= 0) {
        commandRegionDecision(false);
        return;
    }
    if (!view_.hasSelection()) return;
    if (studioMode_) { commandManualSelection(1); return; }

    player_.stop();
    const int64_t start = view_.selectionStart();
    const int64_t end = view_.selectionEnd();

    if (!document_.deleteRange(start, end)) return;

    view_.clearSelection();
    view_.setPlayhead(start);
    markDocumentChanged(true);
    setStatusMessage("selezione eliminata e buco chiuso");
}

void EditorWindow::commandCutSelection() {
    if (studioMode_) { commandManualSelection(1); return; }
    if (!view_.hasSelection()) return;

    player_.stop();
    const int64_t start = view_.selectionStart();
    const int64_t end = view_.selectionEnd();
    const int64_t inizio = std::max<int64_t>(0, start);
    const int64_t fine = std::min<int64_t>(end, document_.frameCount());

    // Si tiene una copia dei campioni tagliati. Sul file il risultato e' lo
    // stesso di "elimina", ma il materiale resta in memoria: e' cio' che
    // distingue un taglio da una cancellazione.
    const AudioBuffer& audio = document_.audio();
    const int canali = std::max(1, audio.channels);
    tagliato_.assign(static_cast<size_t>(std::max<int64_t>(0, fine - inizio) * canali), 0.0f);
    for (int64_t f = inizio; f < fine; ++f) {
        for (int ch = 0; ch < canali; ++ch) {
            const size_t da = static_cast<size_t>(f * canali + ch);
            if (da >= audio.samples.size()) continue;
            tagliato_[static_cast<size_t>((f - inizio) * canali + ch)] = audio.samples[da];
        }
    }
    tagliatoCanali_ = canali;

    if (!document_.deleteRange(start, end)) return;

    view_.clearSelection();
    view_.setPlayhead(start);
    markDocumentChanged(true);

    char messaggio[192];
    snprintf(messaggio, sizeof(messaggio),
             "tagliati %.3f s - buco chiuso, campioni tenuti in memoria",
             static_cast<double>(fine - inizio) /
                 std::max(1, document_.sampleRate()));
    setStatusMessage(messaggio);
}

void EditorWindow::commandSilenceSelection() {
    if (studioMode_) { commandManualSelection(3); return; }
    if (!view_.hasSelection()) return;

    player_.stop();
    if (!document_.silenceRange(view_.selectionStart(), view_.selectionEnd())) return;

    view_.clearSelection();
    markDocumentChanged(false);
    setStatusMessage("selezione silenziata");
}

void EditorWindow::commandCropToSelection() {
    if (!view_.hasSelection()) return;
    if (studioMode_) {
        document_.beginUndoGroup("Ritaglia con piano");
        const int64_t start = view_.selectionStart(), end = view_.selectionEnd();
        if (start > 0) document_.applyManualRange(0, start, 1, editSettings_.breathReductionDb, editFadeMs_);
        if (end < document_.frameCount()) document_.applyManualRange(end, document_.frameCount(), 1, editSettings_.breathReductionDb, editFadeMs_);
        document_.endUndoGroup();
        markDocumentChanged(false);
        return;
    }

    player_.stop();
    if (!document_.cropToRange(view_.selectionStart(), view_.selectionEnd())) return;

    view_.clearSelection();
    view_.setPlayhead(0);
    view_.zoomToFit();
    markDocumentChanged(true);
    setStatusMessage("ritagliato sulla selezione");
}

void EditorWindow::commandSelectAll() {
    if (!document_.isLoaded()) return;

    view_.setSelection(0, document_.frameCount());
    updateStatusBar();
    updateCommandState();
}

void EditorWindow::commandManualSelection(int mode) {
    if (!view_.hasSelection()) { setStatusMessage("SELEZIONE TEMPORALE: trascinare prima un intervallo"); return; }
    const int64_t start = view_.selectionStart(), end = view_.selectionEnd();
    float reduction = editSettings_.breathReductionDb;
    double fade = editFadeMs_;
    for (const auto& region : document_.regions()) {
        if (region.manualOperation && region.startSample == start && region.endSample == end) {
            reduction = region.reductionDb; fade = region.fadeMs; break;
        }
    }
    if (mode == 0 && !showAttenuationDialog(window_, reduction, fade, nullptr)) return;
    commandStop();
    if (mode < 0) {
        const int restored = document_.restoreManualRange(start, end);
        view_.clearRegionSelection();
        markDocumentChanged(false);
        setStatusMessage(std::to_string(restored) + " interventi manuali rimossi; sorgente intatto");
    } else {
        const int index = document_.applyManualRange(start, end, mode, reduction, fade);
        if (index < 0) return;
        view_.setSelectedRegion(index);
        markDocumentChanged(false);
        setStatusMessage(std::string("intervento manuale: ") + studio::editModeName(static_cast<studio::EditMode>(mode)) + "; Undo disponibile");
    }
}

void EditorWindow::commandClicksEnabled(bool enabled) {
    document_.beginUndoGroup(enabled ? "Reincludi click" : "Escludi click");
    for (int i = 0; i < static_cast<int>(document_.regions().size()); ++i) {
        auto region = document_.regions()[i];
        if (region.kind != DetectionKind::Click) continue;
        region.enabled = region.approved = enabled;
        document_.updateRegion(i, region);
    }
    document_.endUndoGroup();
    markDocumentChanged(false);
    setStatusMessage(enabled ? "click inclusi" : "click esclusi (label di training invariate)");
}

void EditorWindow::commandInsertSilence() {
    if (studioMode_) { setStatusMessage("Per mantenere la timeline originale usare Silenzia selezione"); return; }
    if (!document_.isLoaded()) return;

    player_.stop();
    const int64_t campioni =
        static_cast<int64_t>(insertSilenceSeconds_ * document_.sampleRate());
    const int64_t posizione =
        view_.hasSelection() ? view_.selectionStart() : view_.playhead();

    if (!document_.insertSilence(posizione, campioni)) return;

    view_.clearSelection();
    markDocumentChanged(true);

    char messaggio[128];
    snprintf(messaggio, sizeof(messaggio), "inseriti %.2f s di silenzio",
             insertSilenceSeconds_);
    setStatusMessage(messaggio);
}

void EditorWindow::commandShowShortcuts() {
    // Legenda dei tasti. Un messaggio di sistema e' spartano ma si legge e si
    // copia, e per un elenco di scorciatoie non vale la pena progettare una
    // finestra.
    static const wchar_t* testo =
        L"TASTI E SCORCIATOIE\n"
        L"\n"
        L"  EVENTI E CORREZIONI\n"
        L"    Clic su una regione ........ seleziona l'evento\n"
        L"    Trascina il centro ......... sposta l'evento, durata invariata\n"
        L"    Trascina un bordo .......... modifica inizio o fine\n"
        L"    Ctrl+clic .................. selezione multipla\n"
        L"    N / P ...................... evento successivo / precedente\n"
        L"    A / R ...................... includi / escludi evento o gruppo\n"
        L"    Canc ....................... esclude l'evento (non lo elimina)\n"
        L"    L .......................... ascolta il contesto\n"
        L"    G .......................... vai al respiro selezionato\n"
        L"    Ctrl+Z / Ctrl+Y ............ annulla / ripristina\n"
        L"\n"
        L"  PANNELLO CONTESTUALE\n"
        L"    Includi / Escludi .......... stato dell'evento nel piano\n"
        L"    Modifica confini... ........ inizio/fine numerici\n"
        L"    Attenua / Taglia / Dimezza . modalita' dell'evento\n"
        L"    Regioni -> Modalita' globale applica a tutti gli inclusi\n"
        L"\n"
        L"  ZOOM E VISTA\n"
        L"    Rotella o Ctrl+rotella ..... zoom orizzontale sul puntatore\n"
        L"    +  /  - .................... zoom orizzontale\n"
        L"    Adatta ..................... torna alla vista completa\n"
        L"    1x / 2x / 4x / 8x .......... ampiezza verticale (solo vista)\n"
        L"    (l'ampiezza non cambia il guadagno audio)\n"
        L"\n"
        L"  FILE, ANALISI E PROGETTO\n"
        L"    Ctrl+O ..................... apri file\n"
        L"    Ctrl+Shift+O ............... apri cartella WAV\n"
        L"    Ctrl+R o F5 ................ analizza il file\n"
        L"    F2 ......................... impostazioni di rilevamento\n"
        L"    Ctrl+S ..................... salva con nome\n"
        L"    Ctrl+E ..................... esporta audio corretto\n"
        L"    Ctrl+Shift+S ............... salva progetto\n"
        L"    Ctrl+PagSu / Ctrl+PagGiu ... file precedente / successivo\n"
        L"\n"
        L"  ASCOLTO\n"
        L"    Spazio ..................... avvia / ferma\n"
        L"    Home / Fine ................ inizio / fine\n"
        L"    Clic sul righello .......... sposta la testina\n"
        L"\n"
        L"  AIUTO\n"
        L"    F1 ......................... questa legenda\n";

    MessageBoxW(window_, testo, L"NL Breath Click Studio - tasti e scorciatoie",
                MB_OK | MB_ICONINFORMATION);
}

void EditorWindow::commandAbout() {
    showAboutDialog(window_);
}

// ===========================================================================
//  Comandi: regioni
// ===========================================================================
void EditorWindow::commandRegionDelete() {
    const int indice = view_.selectedRegion();
    if (indice < 0) return;

    document_.beginUndoGroup("Elimina regione");
    // Journal editoriale: la cancellazione non certifica una label di training.
    const std::vector<EditRegion>& regioni = document_.regions();
    if (indice < static_cast<int>(regioni.size())) {
        const EditRegion& r = regioni[static_cast<size_t>(indice)];
        document_.recordCorrection(r.kind, r.startSample, r.endSample, false,
                                   "cancellazione");
    }

    if (!document_.removeRegion(indice)) { document_.endUndoGroup(); return; }
    document_.endUndoGroup();

    view_.setSelectedRegion(-1);
    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage("regione eliminata");
}

void EditorWindow::commandRegionToggleEnabled() {
    const int indice = view_.selectedRegion();
    if (indice < 0 || indice >= static_cast<int>(document_.regions().size())) return;

    const EditRegion& current = document_.regions()[static_cast<size_t>(indice)];
    const bool attiva = current.enabled;
    EditRegion updated = current;
    updated.enabled = !attiva;
    updated.approved = !attiva;
    if (!document_.updateRegion(indice, updated)) return;
    hasPreview_ = false;

    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage(attiva ? "evento escluso (resta nel progetto)" : "evento incluso");
}

void EditorWindow::commandRegionSetKind(DetectionKind kind) {
    const int indice = view_.selectedRegion();
    if (indice < 0 || indice >= static_cast<int>(document_.regions().size())) return;

    EditRegion regione = document_.regions()[static_cast<size_t>(indice)];
    regione.kind = kind;
    if (kind == DetectionKind::Click) {
        regione.reductionDb = editSettings_.clickReductionDb;
    } else {
        regione.reductionDb = editSettings_.breathReductionDb;
        regione.repair = false;
    }

    if (!document_.updateRegion(indice, regione)) return;

    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage(kind == DetectionKind::Breath ? "regione: respiro"
                                                   : "regione: click");
}

void EditorWindow::commandRegionGain(float deltaDb) {
    const int indice = view_.selectedRegion();
    if (indice < 0 || indice >= static_cast<int>(document_.regions().size())) return;

    EditRegion regione = document_.regions()[static_cast<size_t>(indice)];
    // L'attenuazione e' <= 0: "di piu'" significa piu' negativa.
    regione.reductionDb = std::max(-60.0f, std::min(0.0f, regione.reductionDb + deltaDb));

    if (!document_.updateRegion(indice, regione)) return;

    view_.refreshDocument(false);
    markDocumentChanged(false);

    char messaggio[64];
    snprintf(messaggio, sizeof(messaggio), "attenuazione: %.1f dB",
             static_cast<double>(regione.reductionDb));
    setStatusMessage(messaggio);
}

void EditorWindow::commandRegionDecision(bool confirm) {
    std::vector<int> indices = view_.selectedRegions();
    if (indices.empty()) {
        int index = view_.selectedRegion();
        if (index < 0) index = document_.regionIndexAt(view_.playhead());
        if (index >= 0) indices.push_back(index);
    }
    if (indices.empty()) {
        setStatusMessage("selezionare almeno un evento");
        return;
    }
    int changed = 0;
    document_.beginUndoGroup("Inclusione regioni");
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(document_.regions().size())) continue;
        EditRegion region = document_.regions()[static_cast<size_t>(index)];
        region.enabled = confirm;
        region.approved = confirm;
        region.decisionSource = "user";
        document_.updateRegion(index, region);
        document_.recordCorrection(region.kind, region.startSample, region.endSample,
                                   confirm, confirm ? "inclusione utente" : "esclusione utente");
        ++changed;
    }
    document_.endUndoGroup();
    hasPreview_ = false;
    view_.refreshDocument(false);
    char message[96] = {};
    std::snprintf(message, sizeof(message), "%d eventi %s",
                  changed, confirm ? "inclusi" : "esclusi (restano nel progetto)");
    setStatusMessage(message);
    markDocumentChanged(false);
}

void EditorWindow::commandRegionSelectAll() {
    view_.selectAllRegions();
    setStatusMessage("tutti gli eventi selezionati");
    updateStatusBar();
}

void EditorWindow::commandRegionClearSelection() {
    view_.clearRegionSelection();
    setStatusMessage("selezione eventi vuota");
    updateStatusBar();
}

void EditorWindow::commandRegionDecisionAll(bool approve) {
    if (!document_.isLoaded()) return;
    int changed = 0;
    document_.beginUndoGroup("Inclusione globale");
    for (size_t i = 0; i < document_.regions().size(); ++i) {
        EditRegion region = document_.regions()[i];
        if (region.approved == approve) continue;
        region.approved = approve;
        region.enabled = approve;
        region.decisionSource = "user";
        document_.updateRegion(static_cast<int>(i), region);
        document_.recordCorrection(region.kind, region.startSample, region.endSample,
                                   approve, approve ? "inclusione gruppo" : "esclusione gruppo");
        ++changed;
    }
    document_.endUndoGroup();
    hasPreview_ = false;
    view_.refreshDocument(false);
    char message[96] = {};
    std::snprintf(message, sizeof(message), "%d eventi %s", changed,
                  approve ? "inclusi" : "esclusi");
    setStatusMessage(message);
    markDocumentChanged(false);
}

void EditorWindow::commandRegionGainReset() {
    const int indice = view_.selectedRegion();
    if (indice < 0 || indice >= static_cast<int>(document_.regions().size())) return;

    EditRegion regione = document_.regions()[static_cast<size_t>(indice)];
    regione.reductionDb = regione.kind == DetectionKind::Breath
                              ? editSettings_.breathReductionDb
                              : editSettings_.clickReductionDb;

    if (!document_.updateRegion(indice, regione)) return;

    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage("attenuazione riportata al valore predefinito");
}

void EditorWindow::commandRegionToggleRepair() {
    const int indice = view_.selectedRegion();
    if (indice < 0 || indice >= static_cast<int>(document_.regions().size())) return;

    EditRegion regione = document_.regions()[static_cast<size_t>(indice)];
    regione.repair = !regione.repair;

    if (!document_.updateRegion(indice, regione)) return;

    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage(regione.repair ? "click da riparare per interpolazione"
                                    : "click da attenuare");
}

void EditorWindow::commandShortenRegions(float keepFraction) {
    if (!document_.isLoaded()) return;
    if (studioMode_) {
        document_.beginUndoGroup("Riduci durata respiri");
        for (int i = 0; i < static_cast<int>(document_.regions().size()); ++i) {
            auto r = document_.regions()[i];
            if (r.kind != DetectionKind::Breath || !r.enabled || !r.approved || r.manualOperation) continue;
            r.editMode = 2; r.keepFraction = keepFraction;
            document_.updateRegion(i, r);
        }
        document_.endUndoGroup(); markDocumentChanged(false); return;
    }

    // Si lavora sui FIATI, che sono quelli che allungano gli spot.
    const int count = document_.shortenRegions(DetectionKind::Breath, keepFraction, true);
    if (count <= 0) {
        setStatusMessage("nessun fiato da accorciare");
        return;
    }

    // Le posizioni delle regioni sono cambiate sotto: si azzera la selezione
    // per non lasciare un intervallo che non corrisponde piu' a nulla.
    view_.clearSelection();
    view_.setSelectedRegion(-1);
    view_.refreshDocument(true);
    markDocumentChanged(true);

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "%d fiati accorciati al %d%% - il resto e' stato spostato indietro",
             count, static_cast<int>(keepFraction * 100.0f + 0.5f));
    setStatusMessage(buffer);
}

void EditorWindow::commandLearningLabel(LearningLabel label) {
    std::vector<int> indices = view_.selectedRegions();
    if (indices.empty() && view_.selectedRegion() >= 0) indices.push_back(view_.selectedRegion());
    if (indices.empty()) {
        setStatusMessage("selezionare almeno un evento da etichettare");
        return;
    }
    if (label == LearningLabel::MissedBreath || label == LearningLabel::MissedClick) {
        bool hasManual = false;
        for (int index : indices) {
            if (index >= 0 && index < static_cast<int>(document_.regions().size()) &&
                !document_.regions()[static_cast<size_t>(index)].fromDetection) {
                hasManual = true;
            }
        }
        if (!hasManual) {
            setStatusMessage("per un evento mancato aggiungere prima una regione manuale");
            return;
        }
    }
    document_.beginUndoGroup("Etichetta apprendimento");
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(document_.regions().size())) continue;
        EditRegion region = document_.regions()[static_cast<size_t>(index)];
        const bool clickLabel = label==LearningLabel::ClickConfirmed || label==LearningLabel::ClickFalsePositive ||
            label==LearningLabel::MissedClick || label==LearningLabel::MouthClickConfirmed || label==LearningLabel::VocalTransient;
        if(label==LearningLabel::MissedClick && !region.fromDetection)region.kind=DetectionKind::Click;
        if (label!=LearningLabel::EditorialExclusion && label!=LearningLabel::Unverified &&
            ((clickLabel && region.kind!=DetectionKind::Click) || (!clickLabel && region.kind!=DetectionKind::Breath))) continue;
        if (clickLabel) { region.manualOperation=false; region.repair=true; region.editMode=0; }
        if (region.manualOperation && label!=LearningLabel::EditorialExclusion && label!=LearningLabel::Unverified) continue;
        region.learningLabel = learningLabelName(label);
        switch (label) {
            case LearningLabel::ClickConfirmed:
            case LearningLabel::MissedClick:
            case LearningLabel::MouthClickConfirmed:
            case LearningLabel::BreathConfirmed:
            case LearningLabel::MissedBreath:
                region.enabled = true;
                region.approved = true;
                region.trainingEligible = true;
                region.decisionSource = "user";
                break;
            case LearningLabel::ClickFalsePositive:
            case LearningLabel::VocalTransient:
            case LearningLabel::FalsePositive:
                region.enabled = false;
                region.approved = false;
                region.trainingEligible = true;
                region.decisionSource = "user";
                break;
            case LearningLabel::EditorialExclusion:
                region.enabled = false;
                region.approved = false;
                region.trainingEligible = false;
                region.decisionSource = "user";
                break;
            case LearningLabel::Unverified:
            default:
                region.learningLabel.clear();
                region.trainingEligible = false;
                break;
        }
        document_.updateRegion(index, region);
        if (label == LearningLabel::BreathConfirmed || label == LearningLabel::MissedBreath) {
            document_.recordCorrection(region.kind, region.startSample, region.endSample,
                                       true, "training: " + region.learningLabel);
        } else if (label == LearningLabel::FalsePositive) {
            document_.recordCorrection(region.kind, region.startSample, region.endSample,
                                       false, "training: false_positive");
        }
    }
    document_.endUndoGroup();
    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage(std::string("etichetta: ") + learningLabelName(label));
}

void EditorWindow::commandTrainPersonalProfile() {
    std::vector<LearningExample> examples = collectLearningExamples(document_);
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        if (i == activeTrack_) continue;
        const AudioDocument* track = trackDocument(i);
        if (!track || !track->isLoaded()) continue;
        std::vector<LearningExample> more = collectLearningExamples(*track);
        examples.insert(examples.end(), more.begin(), more.end());
    }
    std::string error;
    PersonalProfile trained;
    if (!trainPersonalProfile(examples, trained, &error)) {
        MessageBoxW(window_, toWide("Profilo non creato:\n\n" + error).c_str(),
                    L"Apprendimento dalle correzioni", MB_OK | MB_ICONINFORMATION);
        setStatusMessage("profilo non creato: " + error);
        return;
    }
    const std::string path = detectorSettingsDirectory() + "\\personal_profile.ini";
    if (!savePersonalProfile(path, trained, &error)) {
        MessageBoxW(window_, toWide("Profilo non salvato:\n\n" + error).c_str(),
                    L"Apprendimento dalle correzioni", MB_OK | MB_ICONERROR);
        return;
    }
    personalProfile_ = trained;
    personalProfileActive_ = false;
    wchar_t message[768] = {};
    swprintf(message, 768,
             L"Profilo personale creato e validato.\n\n"
             L"Positivi: %d, negativi: %d, esempi di validazione: %d\n"
             L"Accuratezza bilanciata sul file separato: %.2f (proposte generali %.2f)\n\n"
             L"Mancati: %d (generale %d), falsi positivi: %d (generale %d).\n"
             L"Il calibratore filtra le proposte; i respiri mancati restano da aggiungere.\n\n"
             L"Il profilo NON modifica i pesi Respiro-en. Attivalo dal menu "
             L"Apprendimento per applicarlo alle prossime analisi.",
             trained.positives, trained.negatives, trained.validationExamples,
             trained.validationBalancedAccuracy, trained.baselineBalancedAccuracy,
             trained.validationMisses, trained.baselineMisses,
             trained.validationFalsePositives, trained.baselineFalsePositives);
    MessageBoxW(window_, message, L"Profilo personale creato",
                MB_OK | MB_ICONINFORMATION);
    setStatusMessage("profilo personale creato; attivalo dal menu Apprendimento");
}

void EditorWindow::commandActivatePersonalProfile(bool activate) {
    if (activate && !personalProfile_.valid) {
        const std::string path = detectorSettingsDirectory() + "\\personal_profile.ini";
        std::string error;
        if (!loadPersonalProfile(path, personalProfile_, &error)) {
            MessageBoxW(window_, toWide("Nessun profilo personale valido:\n\n" + error).c_str(),
                        L"Profilo personale", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    personalProfileActive_ = activate && personalProfile_.valid;
    setStatusMessage(personalProfileActive_ ? "profilo personale attivo"
                                            : "profilo generale attivo");
    updateCommandState();
    updateStatusBar();
}

void EditorWindow::commandLearningStatus() {
    if (!personalProfile_.valid) {
        MessageBoxW(window_, L"Nessun profilo personale valido.\n\n"
                              L"Le correzioni sono raccolte, ma il profilo viene creato "
                              L"solo con esempi esplicitamente confermati da almeno due "
                              L"registrazioni e con validazione indipendente.",
                    L"Apprendimento dalle correzioni", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t message[512] = {};
    swprintf(message, 512,
             L"Profilo personale %s\n\n"
             L"Creato: %hs\n"
             L"Positivi: %d, negativi: %d\n"
             L"Validazione: %d esempi, accuratezza bilanciata %.2f (generale %.2f)\n"
             L"Soglia calibrata: %.2f\n\n"
             L"Nota: questo e' un calibratore personale su score/feature DSP. "
             L"I pesi ONNX di Respiro-en non vengono modificati.",
             personalProfileActive_ ? L"ATTIVO" : L"non attivo",
             personalProfile_.createdAt.c_str(), personalProfile_.positives,
             personalProfile_.negatives, personalProfile_.validationExamples,
             personalProfile_.validationBalancedAccuracy,
             personalProfile_.baselineBalancedAccuracy, personalProfile_.threshold);
    MessageBoxW(window_, message, L"Apprendimento dalle correzioni",
                MB_OK | MB_ICONINFORMATION);
}

void EditorWindow::commandRegionAddAtSelection() {
    if (!view_.hasSelection()) return;

    EditRegion regione;
    regione.kind = DetectionKind::Breath;
    regione.startSample = view_.selectionStart();
    regione.endSample = view_.selectionEnd();
    regione.reductionDb = editSettings_.breathReductionDb;
    regione.fromDetection = false;   // aggiunta manuale, non label di training
    regione.approved = true;          // inclusa subito nel piano
    regione.enabled = true;
    regione.decisionSource = "user";
    regione.trainingEligible = false;
    regione.editMode = defaultEditMode_;
    regione.fadeMs = editFadeMs_;

    const int indice = document_.addRegion(regione);
    if (indice < 0) return;

    view_.setSelectedRegion(indice);
    view_.refreshDocument(false);
    markDocumentChanged(false);
    setStatusMessage("regione aggiunta sulla selezione");
}

// ===========================================================================
//  Comandi: vista
// ===========================================================================
void EditorWindow::commandZoomIn() {
    if (!document_.isLoaded()) return;
    int64_t anchor = view_.hasSelection() ? view_.selectionStart() : view_.playhead();
    const int region = view_.selectedRegion();
    if (region >= 0 && region < static_cast<int>(document_.regions().size())) {
        const EditRegion& r = document_.regions()[static_cast<size_t>(region)];
        anchor = (r.startSample + r.endSample) / 2;
    }
    view_.zoomAround(0.5, anchor);
    updateStatusBar();
}

void EditorWindow::commandZoomOut() {
    if (!document_.isLoaded()) return;
    int64_t anchor = view_.hasSelection() ? view_.selectionStart() : view_.playhead();
    const int region = view_.selectedRegion();
    if (region >= 0 && region < static_cast<int>(document_.regions().size())) {
        const EditRegion& r = document_.regions()[static_cast<size_t>(region)];
        anchor = (r.startSample + r.endSample) / 2;
    }
    view_.zoomAround(2.0, anchor);
    updateStatusBar();
}

void EditorWindow::commandZoomFit() {
    if (!document_.isLoaded()) return;
    view_.zoomToFit();
    updateStatusBar();
}

void EditorWindow::commandZoomSelection() {
    if (!view_.hasSelection()) {
        setStatusMessage("nessuna selezione da ingrandire");
        return;
    }
    // Si centra la selezione e la si porta a occupare circa un terzo della
    // vista, che e' il compromesso comodo per lavorarci sopra.
    const int64_t lunghezza = view_.selectionEnd() - view_.selectionStart();
    if (lunghezza <= 0) return;

    view_.setSelection(view_.selectionStart(), view_.selectionEnd());
    view_.zoomToFit();
    setStatusMessage("zoom sulla selezione");
    updateStatusBar();
}

// ===========================================================================
//  Comandi: trasporto
// ===========================================================================
void EditorWindow::commandPlay() {
    if (!document_.isLoaded()) return;
    if (player_.isPlaying()) {
        commandStop();
        return;
    }

    if (studioMode_) {
        commandPlayMode(abOriginal_);
        return;
    }

    renderIfNeeded(true);
    if (!ensurePlayerOpen()) return;

    player_.play(view_.hasSelection() ? view_.selectionStart() : view_.playhead());
    updateCommandState();
}

void EditorWindow::commandPlaySelection() {
    if (!view_.hasSelection()) {
        commandPlay();
        return;
    }
    if (studioMode_) {
        commandStop();
        commandPlayMode(hasPreview_ ? abOriginal_ : true);
        return;
    }

    renderIfNeeded(true);
    if (!ensurePlayerOpen()) return;

    view_.setPlayhead(view_.selectionStart());
    player_.play(view_.selectionStart());
    updateCommandState();
}

void EditorWindow::commandPlayMode(bool original) {
    if (!document_.isLoaded()) return;
    const bool wasPlaying = player_.isPlaying();
    const int64_t heardFrame = playerPositionInDocument();
    if (wasPlaying) commandStop();

    if (original) {
        if (!originalAudio_ || originalAudio_->size() != document_.audio().samples.size()) {
            auto copy = std::make_shared<std::vector<float>>(document_.audio().samples);
            originalAudio_ = copy;
        }
        if (!ensurePlayerOpen()) return;
        player_.setAudio(originalAudio_, document_.frameCount());
        playerUsesPreview_ = false;
        abOriginal_ = true;
        setStatusMessage("ascolto ORIGINALE");
    } else if (studioMode_) {
        if (!hasPreview_) commandPreview();
        if (!hasPreview_) return;
        if (!ensurePlayerOpen()) return;
        player_.setAudio(previewAudio_, preview_.audio.frameCount());
        playerUsesPreview_ = true;
        abOriginal_ = false;
        setStatusMessage("ascolto ELABORATO (anteprima)");
    } else {
        renderIfNeeded(true);
        if (!ensurePlayerOpen()) return;
        abOriginal_ = false;
        setStatusMessage("ascolto ELABORATO");
    }
    int64_t startFrame = wasPlaying ? heardFrame :
        (view_.hasSelection() ? view_.selectionStart() : view_.playhead());
    if (!original && studioMode_ && hasPreview_) {
        startFrame = preview_.map.mapOriginal(startFrame);
    }
    player_.play(startFrame);
    updateCommandState();
}

int64_t EditorWindow::playerPositionInDocument() const {
    const int64_t frame = player_.position();
    return playerUsesPreview_ ? preview_.map.mapEdited(frame) : frame;
}

void EditorWindow::commandStop() {
    if (player_.isPlaying()) view_.setPlayhead(playerPositionInDocument());
    player_.stop();
    updateCommandState();
}

void EditorWindow::commandGoToStart() {
    view_.setPlayhead(0);
    updateStatusBar();
}

void EditorWindow::commandGoToEnd() {
    if (!document_.isLoaded()) return;
    view_.setPlayhead(document_.frameCount());
    updateStatusBar();
}

// ===========================================================================
//  Comandi: fade
// ===========================================================================
void EditorWindow::commandFade() {
    if (!document_.isLoaded()) return;

    FadeSettings fade = document_.fade();
    fade.inEnabled = true;
    fade.outEnabled = true;
    fade.inSeconds = 0.05;
    fade.outSeconds = 0.05;
    fade.inShape = FadeShape::Cosine;
    fade.outShape = FadeShape::Cosine;

    document_.setFade(fade);
    markDocumentChanged(false);
    setStatusMessage("fade di ingresso e uscita applicati (50 ms)");
}

void EditorWindow::commandClearFade() {
    if (!document_.isLoaded()) return;

    FadeSettings fade = document_.fade();
    fade.inEnabled = false;
    fade.outEnabled = false;
    document_.setFade(fade);

    markDocumentChanged(false);
    setStatusMessage("fade rimossi");
}


} // namespace bc

#endif // _WIN32

