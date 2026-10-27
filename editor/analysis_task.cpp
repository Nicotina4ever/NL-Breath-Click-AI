// SPDX-License-Identifier: MPL-2.0
#include "analysis_task.h"
#ifdef _WIN32
#include <algorithm>
#include <commctrl.h>
#include <mutex>
#include <thread>

namespace bc {
namespace {
constexpr wchar_t kClass[] = L"BreathClickAnalysisProgress";
struct Progress {
    studio::AnalysisControl& control;
    std::mutex mutex;
    std::string stage = "Preparazione";
    float fraction = -1;
    std::atomic<bool> done{false};
    bool success = false;
    HWND label = nullptr, bar = nullptr, cancel = nullptr;
    bool cancelling = false;

    void requestCancel() {
        control.cancelRequested = true;
        if (cancel) EnableWindow(cancel, FALSE);
        if (label) SetWindowTextW(label, L"Annullamento dell'analisi...");
        cancelling = true;
    }
};

LRESULT CALLBACK progressProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* state = reinterpret_cast<Progress*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<Progress*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state) {
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
            state->requestCancel(); return 0;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

std::wstring wide(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring value(count, L'\0');
    if (count) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(), count);
    return value;
}
}

bool analyzeWithProgress(HWND owner, const std::string& path,
                         const studio::Settings& settings, studio::Analysis* result,
                         std::string* error, studio::AnalysisControl& control) {
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    WNDCLASSW wc{}; wc.lpfnWndProc = progressProc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    Progress state{control};
    RECT bounds{}; GetWindowRect(owner, &bounds);
    const HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kClass, L"Analisi della registrazione", WS_CAPTION | WS_SYSMENU | WS_POPUP,
        bounds.left + std::max(0L, (bounds.right - bounds.left - 480) / 2),
        bounds.top + std::max(0L, (bounds.bottom - bounds.top - 175) / 2),
        480, 175, owner, nullptr, instance, &state);
    if (!dialog) { if (error) *error = "Impossibile aprire l'avanzamento dell'analisi"; return false; }
    state.label = CreateWindowExW(0, L"STATIC", L"Preparazione", WS_VISIBLE | WS_CHILD,
        18, 18, 430, 24, dialog, reinterpret_cast<HMENU>(1001), instance, nullptr);
    state.bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_VISIBLE | WS_CHILD | PBS_MARQUEE,
        18, 50, 430, 20, dialog, reinterpret_cast<HMENU>(1002), instance, nullptr);
    state.cancel = CreateWindowExW(0, L"BUTTON", L"Annulla", WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        348, 88, 100, 28, dialog, reinterpret_cast<HMENU>(IDCANCEL), instance, nullptr);
    const auto font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(state.label, WM_SETFONT, font, TRUE); SendMessageW(state.cancel, WM_SETFONT, font, TRUE);
    if (!state.label || !state.bar || !state.cancel) {
        DestroyWindow(dialog); if (error) *error = "Impossibile creare i controlli dell'analisi"; return false;
    }
    control.progress = [&](float fraction, const char* stage) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.fraction = fraction; state.stage = stage;
    };
    std::thread worker;
    try {
        worker = std::thread([&, path, settings] {
            try { state.success = studio::analyzeFile(path, settings, result, error, &control); }
            catch (const std::exception& e) { if (error) *error = e.what(); }
            catch (...) { if (error) *error = "Errore inatteso durante l'analisi"; }
            state.done.store(true, std::memory_order_release);
        });
    } catch (const std::exception& e) {
        control.progress = {}; DestroyWindow(dialog); if (error) *error = e.what(); return false;
    }
    const bool ownerEnabled = IsWindowEnabled(owner) != FALSE;
    const HWND previousFocus = GetFocus();
    struct TaskLifetime {
        HWND owner, dialog, focus;
        bool ownerEnabled;
        studio::AnalysisControl& control;
        std::thread& worker;
        ~TaskLifetime() {
            if (worker.joinable()) { control.cancelRequested = true; worker.join(); }
            control.progress = {};
            if (ownerEnabled && IsWindow(owner)) EnableWindow(owner, TRUE);
            if (IsWindow(dialog)) DestroyWindow(dialog);
            if (IsWindow(focus)) SetFocus(focus);
        }
    } lifetime{owner, dialog, previousFocus, ownerEnabled, control, worker};
    EnableWindow(owner, FALSE); ShowWindow(dialog, SW_SHOW); SetFocus(state.cancel);
    SendMessageW(state.bar, PBM_SETMARQUEE, TRUE, 40);
    bool marquee = true, quitting = false;
    int quitCode = 0;
    while (!state.done.load(std::memory_order_acquire)) {
        if (!state.cancelling && control.cancelled()) state.requestCancel();
        if (!state.cancelling) {
            float fraction; std::string stage;
            { std::lock_guard<std::mutex> lock(state.mutex); fraction = state.fraction; stage = state.stage; }
            SetWindowTextW(state.label, wide(stage).c_str());
            const bool indeterminate = fraction < 0;
            if (marquee != indeterminate) {
                const auto style = GetWindowLongPtrW(state.bar, GWL_STYLE);
                SetWindowLongPtrW(state.bar, GWL_STYLE, indeterminate ? style | PBS_MARQUEE : style & ~PBS_MARQUEE);
                SendMessageW(state.bar, PBM_SETMARQUEE, indeterminate, 40);
                marquee = indeterminate;
            }
            if (!indeterminate) SendMessageW(state.bar, PBM_SETPOS,
                static_cast<WPARAM>(std::clamp(fraction, 0.f, 1.f) * 100), 0);
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                quitting = true; quitCode = static_cast<int>(message.wParam); state.requestCancel(); continue;
            }
            if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                state.requestCancel(); continue;
            }
            if (!IsDialogMessageW(dialog, &message)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 40, QS_ALLINPUT);
    }
    worker.join();
    control.progress = {};
    // A last-moment cancellation must not publish an otherwise complete result.
    if (control.cancelled(error)) state.success = false;
    if (quitting) PostQuitMessage(quitCode);
    return state.success;
}
} // namespace bc
#endif
