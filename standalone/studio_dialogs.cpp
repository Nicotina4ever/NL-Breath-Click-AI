// SPDX-License-Identifier: MPL-2.0
#include "studio_dialogs.h"
#include "version.h"

#ifdef _WIN32

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <cwchar>
#include <cmath>

namespace bc {
namespace {

INT_PTR CALLBACK aboutProc(HWND window, UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(window, 4103, L"Versione " BC_STUDIO_VERSION_W L" - release candidate");
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == IDOK || id == IDCANCEL) { EndDialog(window, id); return TRUE; }
        const wchar_t* target = id == 4101 ? L"mailto:info@nicolalanci.com" :
                                id == 4102 ? L"https://www.nicolalanci.com" : nullptr;
        if (target) {
            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", target, nullptr, nullptr, SW_SHOWNORMAL));
            if (result <= 32) MessageBoxW(window, L"Impossibile aprire il collegamento. Usa l'indirizzo riportato nella finestra Informazioni.",
                                         BC_PRODUCT_NAME_W, MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
    }
    return FALSE;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string result(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring result(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), needed);
    return result;
}

struct AttenuationState {
    HWND window = nullptr;
    HWND reduction = nullptr;
    HWND fade = nullptr;
    HWND crossfade = nullptr;
    HWND error = nullptr;
    HFONT font = nullptr;
    bool accepted = false;
    float* reductionDb = nullptr;
    double* fadeMs = nullptr;
    double* crossfadeMs = nullptr;
};

constexpr wchar_t kClass[] = L"BreathClickAttenuationDialog";
constexpr int kOk = 1;
constexpr int kCancel = 2;

LRESULT CALLBACK dialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AttenuationState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case DM_GETDEFID: return MAKELRESULT(kOk, DC_HASDEFID);
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<AttenuationState*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto label = [&](const wchar_t* text, int x, int y, int w, int h) {
                HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                               x, y, w, h, window, nullptr, nullptr, nullptr);
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                return control;
            };
            label(state->crossfadeMs ? L"Attenuazione respiri (dB, da -60 a 0)"
                                    : L"Attenuazione selezione (dB, da -60 a 0)", 16, 16, 330, 20);
            state->reduction = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"-12",
                                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                               16, 38, 100, 24, window,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(101)), nullptr, nullptr);
            SendMessageW(state->reduction, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);

            label(L"Fade di attacco/rilascio (ms, da 5 a 200)", 16, 74, 310, 20);
            state->fade = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"15",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                          16, 96, 100, 24, window,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(102)), nullptr, nullptr);
            SendMessageW(state->fade, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);

            if (state->crossfadeMs) {
                label(L"Crossfade dei tagli (ms, da 0 a 50)", 16, 128, 330, 20);
                state->crossfade = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    16, 150, 100, 24, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(103)), nullptr, nullptr);
                SendMessageW(state->crossfade, WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->font), TRUE);
            }
            const int buttonY = state->crossfadeMs ? 224 : 174;
            state->error = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                           16, state->crossfadeMs ? 182 : 128, 330, 40,
                                           window, nullptr, nullptr, nullptr);
            SendMessageW(state->error, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);

            HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      170, buttonY, 80, 28, window,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOk)), nullptr, nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"Annulla",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          258, buttonY, 80, 28, window,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancel)), nullptr, nullptr);
            SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SetFocus(state->reduction);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kOk) {
                wchar_t reductionText[32] = {};
                wchar_t fadeText[32] = {};
                wchar_t crossfadeText[32] = {};
                GetWindowTextW(state->reduction, reductionText, 32);
                GetWindowTextW(state->fade, fadeText, 32);
                if (state->crossfade) GetWindowTextW(state->crossfade, crossfadeText, 32);
                wchar_t* end = nullptr;
                const double reduction = std::wcstod(reductionText, &end);
                if (end == reductionText || *end != L'\0' || !std::isfinite(reduction) || reduction < -60.0 || reduction > 0.0) {
                    SetWindowTextW(state->error, L"Attenuazione non valida: usare -60..0 dB.");
                    return 0;
                }
                const double fade = std::wcstod(fadeText, &end);
                if (end == fadeText || *end != L'\0' || !std::isfinite(fade) || fade < 5.0 || fade > 200.0) {
                    SetWindowTextW(state->error, L"Fade non valido: usare 5..200 ms.");
                    return 0;
                }
                const double crossfade = state->crossfadeMs ? std::wcstod(crossfadeText, &end) : 0;
                if (state->crossfadeMs && (end == crossfadeText || *end != L'\0' ||
                    !std::isfinite(crossfade) || crossfade < 0.0 || crossfade > 50.0)) {
                    SetWindowTextW(state->error, L"Crossfade non valido: usare 0..50 ms.");
                    return 0;
                }
                *state->reductionDb = static_cast<float>(reduction);
                *state->fadeMs = fade;
                if (state->crossfadeMs) *state->crossfadeMs = crossfade;
                state->accepted = true;
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wParam) == kCancel) {
                DestroyWindow(window);
                return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            // This is a nested modal loop on the editor thread. Posting
            // WM_QUIT here would also terminate the main application loop.
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

void showAboutDialog(HWND parent) {
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(4100), parent, aboutProc, 0) == -1)
        MessageBoxW(parent, BC_PRODUCT_NAME_W L" " BC_STUDIO_VERSION_W L"\n\n" BC_AUTHOR_W
                    L"\n" BC_EMAIL_W L"\n" BC_WEBSITE_W L"\n\nMozilla Public License 2.0 (MPL-2.0)",
                    L"Informazioni su " BC_PRODUCT_NAME_W, MB_OK | MB_ICONINFORMATION);
}

bool showAttenuationDialog(HWND parent, float& reductionDb, double& fadeMs,
                           double* crossfadeMs) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = dialogProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    AttenuationState state;
    state.reductionDb = &reductionDb;
    state.fadeMs = &fadeMs;
    state.crossfadeMs = crossfadeMs;

    RECT owner = {};
    if (parent) GetWindowRect(parent, &owner);
    const int width = 370;
    const int height = crossfadeMs ? 300 : 250;
    const int x = parent ? owner.left + 80 : CW_USEDEFAULT;
    const int y = parent ? owner.top + 80 : CW_USEDEFAULT;

    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Attenuazione e fade",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, parent, nullptr,
                                  GetModuleHandleW(nullptr), &state);
    if (!window) return false;

    wchar_t reduction[32] = {};
    wchar_t fade[32] = {};
    wchar_t crossfade[32] = {};
    std::swprintf(reduction, 32, L"%.1f", reductionDb);
    std::swprintf(fade, 32, L"%.0f", fadeMs);
    std::swprintf(crossfade, 32, L"%.0f", crossfadeMs ? *crossfadeMs : 0.0);
    SetWindowTextW(state.reduction, reduction);
    SetWindowTextW(state.fade, fade);
    if (state.crossfade) SetWindowTextW(state.crossfade, crossfade);

    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetFocus(state.reduction);
    SendMessageW(state.reduction, EM_SETSEL, 0, -1);

    MSG message = {};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (parent) EnableWindow(parent, TRUE);
    return state.accepted;
}

bool showKeepFractionDialog(HWND parent, double& keepFraction) {
    // Riusa lo stesso stile del dialogo attenuazione; una sola casella.
    struct State {
        HWND window = nullptr;
        HWND edit = nullptr;
        HWND error = nullptr;
        HFONT font = nullptr;
        bool accepted = false;
        double* keep = nullptr;
    };
    static constexpr wchar_t kKeepClass[] = L"BreathClickKeepDialog";
    static State* current = nullptr;
    auto proc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        switch (message) {
            case DM_GETDEFID: return MAKELRESULT(kOk, DC_HASDEFID);
            case WM_CREATE: {
                auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
                state = reinterpret_cast<State*>(create->lpCreateParams);
                state->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                HWND label = CreateWindowExW(0, L"STATIC",
                    L"Percentuale del respiro da conservare (0-100):",
                    WS_CHILD | WS_VISIBLE, 16, 16, 330, 20, window, nullptr, nullptr, nullptr);
                SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"50",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 16, 40, 100, 24, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(101)), nullptr, nullptr);
                SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                state->error = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                    16, 74, 330, 40, window, nullptr, nullptr, nullptr);
                SendMessageW(state->error, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 170, 130, 80, 28, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOk)), nullptr, nullptr);
                HWND cancel = CreateWindowExW(0, L"BUTTON", L"Annulla",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, 258, 130, 80, 28, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancel)), nullptr, nullptr);
                SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                SetFocus(state->edit);
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(wParam) == kOk) {
                    wchar_t text[32] = {};
                    GetWindowTextW(state->edit, text, 32);
                    wchar_t* end = nullptr;
                    const double value = std::wcstod(text, &end);
                    if (end == text || *end != L'\0' || !std::isfinite(value) || value < 0.0 || value > 100.0) {
                        SetWindowTextW(state->error, L"Valore non valido: usare 0..100.");
                        return 0;
                    }
                    *state->keep = value / 100.0;
                    state->accepted = true;
                    DestroyWindow(window);
                    return 0;
                }
                if (LOWORD(wParam) == kCancel) {
                    DestroyWindow(window);
                    return 0;
                }
                return 0;
            case WM_CLOSE: DestroyWindow(window); return 0;
            case WM_DESTROY: return 0;
            default: return DefWindowProcW(window, message, wParam, lParam);
        }
    };
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kKeepClass;
    RegisterClassExW(&wc);
    State state;
    state.keep = &keepFraction;
    current = &state;
    RECT owner = {};
    if (parent) GetWindowRect(parent, &owner);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kKeepClass, L"Dimezza respiro",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, parent ? owner.left + 80 : CW_USEDEFAULT,
        parent ? owner.top + 80 : CW_USEDEFAULT, 370, 210, parent, nullptr,
        GetModuleHandleW(nullptr), &state);
    if (!window) { current = nullptr; return false; }
    wchar_t text[32] = {};
    std::swprintf(text, 32, L"%.0f", keepFraction * 100.0);
    SetWindowTextW(state.edit, text);
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
    current = nullptr;
    return state.accepted;
}

bool chooseDirectory(HWND parent, const std::wstring& title, std::string& out) {
    // Dialogo moderno di Windows: stessa navigazione di Esplora file, barra
    // del percorso modificabile, dischi e cartelle sincronizzate.
    static std::wstring lastFolder;
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(comResult);
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) {
        if (comInitialized) CoUninitialize();
        return false;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    if (!title.empty()) dialog->SetTitle(title.c_str());
    if (!lastFolder.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(lastFolder.c_str(), nullptr,
                                                  IID_PPV_ARGS(&folder))) && folder) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    bool result = false;
    if (dialog->Show(parent) == S_OK) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                out = toUtf8(path);
                lastFolder = path;
                CoTaskMemFree(path);
                result = !out.empty();
            }
            item->Release();
        }
    }
    dialog->Release();
    if (comInitialized) CoUninitialize();
    return result;
}

bool chooseFilePath(HWND parent, bool save, const wchar_t* filter,
                    const wchar_t* defaultExtension, std::string& out) {
    wchar_t path[MAX_PATH] = {};
    if (!out.empty()) {
        const std::wstring wide = toWide(out);
        wcsncpy_s(path, wide.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = parent;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = defaultExtension;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!ok) return false;
    out = toUtf8(path);
    return true;
}

} // namespace bc

#endif
