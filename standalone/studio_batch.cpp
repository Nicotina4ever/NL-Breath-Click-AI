// SPDX-License-Identifier: MPL-2.0
#include "studio_batch.h"

#ifdef _WIN32

#include <commctrl.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "studio_engine.h"

namespace bc {
namespace {

constexpr wchar_t kClass[] = L"BreathClickBatchWindow";
constexpr int kStart = 1;
constexpr int kClose = 2;
constexpr int kList = 100;
constexpr int kProgress = 101;
constexpr int kStatus = 102;

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), needed);
    return out;
}

void pump() {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

struct BatchState {
    HWND window = nullptr;
    HWND list = nullptr;
    HWND progress = nullptr;
    HWND status = nullptr;
    HWND start = nullptr;
    HFONT font = nullptr;
    std::string inputDirectory;
    studio::Settings settings;
    std::vector<std::filesystem::path> files;
    bool done = false;
};

void addListRow(BatchState* state, const std::wstring& file, const std::wstring& status) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(state->list);
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(file.c_str());
    const int index = ListView_InsertItem(state->list, &item);
    ListView_SetItemText(state->list, index, 1, const_cast<wchar_t*>(status.c_str()));
}

void addListResult(BatchState* state, const std::wstring& file, int events,
                   const std::wstring& status) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(state->list);
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(file.c_str());
    const int index = ListView_InsertItem(state->list, &item);
    const std::wstring count = std::to_wstring(events);
    ListView_SetItemText(state->list, index, 1, const_cast<wchar_t*>(count.c_str()));
    ListView_SetItemText(state->list, index, 2, const_cast<wchar_t*>(status.c_str()));
}

void scan(BatchState* state) {
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::u8path(state->inputDirectory);
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) {
            const std::string dirName = it->path().filename().u8string();
            if (dirName.rfind("breathclick_out", 0) == 0) {
                it.disable_recursion_pending();
                continue;
            }
        }
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".wav") state->files.push_back(it->path());
    }
    std::sort(state->files.begin(), state->files.end());
}

void runBatch(BatchState* state) {
    if (state->files.empty()) scan(state);
    const int total = static_cast<int>(state->files.size());
    ListView_DeleteAllItems(state->list);
    SendMessageW(state->progress, PBM_SETRANGE32, 0, std::max(1, total));
    SendMessageW(state->progress, PBM_SETPOS, 0, 0);
    SetWindowTextW(state->status,
                   (L"File trovati: " + std::to_wstring(total) + L". Elaborazione...").c_str());
    EnableWindow(state->start, FALSE);

    const std::filesystem::path outDir =
        std::filesystem::u8path(state->inputDirectory) / "breathclick_out";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    std::ostringstream errors;
    int success = 0;

    for (int i = 0; i < total; ++i) {
        const auto& file = state->files[static_cast<size_t>(i)];
        const std::wstring name = file.filename().wstring();
        SetWindowTextW(state->status, (L"Elaborazione: " + name).c_str());
        pump();

        studio::Report report;
        std::string error;
        const bool ok = studio::processFile(file.u8string(), outDir.u8string(),
                                            state->settings, &report, &error);
        if (!ok) {
            errors << file.u8string() << ": " << error << "\n";
            addListResult(state, name, 0, L"ERRORE: " + toWide(error));
        } else {
            const std::string reportPath =
                (outDir / (file.stem().string() + ".report.json")).u8string();
            std::string reportError;
            studio::writeReportJson(reportPath, report, &reportError);
            studio::writeReportText(reportPath + ".txt", report, &reportError);
            addListResult(state, name, report.processedEvents,
                          L"OK, fuori max " + toWide(std::to_string(report.outsideMaxDeltaDb)) + L" dB");
            ++success;
        }
        SendMessageW(state->progress, PBM_SETPOS, i + 1, 0);
        pump();
    }

    std::ofstream summary(outDir / "batch_report.json", std::ios::binary);
    summary << "{\n  \"schema\": 1,\n  \"input\": \""
            << state->inputDirectory << "\",\n"
            << "  \"files\": " << total << ",\n"
            << "  \"success\": " << success << ",\n"
            << "  \"errors\": \"" << errors.str() << "\"\n}\n";

    SetWindowTextW(state->status,
                   (L"Completato: " + std::to_wstring(success) + L"/" +
                    std::to_wstring(total) + L" file. Cartella: " + outDir.wstring()).c_str());
    EnableWindow(state->start, TRUE);
}

LRESULT CALLBACK batchProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<BatchState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<BatchState*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            state->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          12, 12, 700, 300, window,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kList)), nullptr, nullptr);
            ListView_SetExtendedListViewStyle(state->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.iSubItem = 0;
            column.cx = 360;
            column.pszText = const_cast<wchar_t*>(L"File");
            ListView_InsertColumn(state->list, 0, &column);
            column.iSubItem = 1;
            column.cx = 70;
            column.pszText = const_cast<wchar_t*>(L"Respiri");
            ListView_InsertColumn(state->list, 1, &column);
            column.iSubItem = 2;
            column.cx = 260;
            column.pszText = const_cast<wchar_t*>(L"Stato");
            ListView_InsertColumn(state->list, 2, &column);
            SendMessageW(state->list, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);

            state->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                              WS_CHILD | WS_VISIBLE,
                                              12, 322, 700, 20, window,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProgress)), nullptr, nullptr);
            state->status = CreateWindowExW(0, L"STATIC", L"Pronto.",
                                            WS_CHILD | WS_VISIBLE,
                                            12, 350, 700, 36, window, nullptr, nullptr, nullptr);
            SendMessageW(state->status, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);
            state->start = CreateWindowExW(0, L"BUTTON", L"Avvia batch",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           520, 390, 110, 30, window,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStart)), nullptr, nullptr);
            HWND close = CreateWindowExW(0, L"BUTTON", L"Chiudi",
                                         WS_CHILD | WS_VISIBLE,
                                         638, 390, 80, 30, window,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClose)), nullptr, nullptr);
            SendMessageW(state->start, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(close, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);

            state->files.clear();
            scan(state);
            for (const auto& file : state->files) {
                addListRow(state, file.filename().wstring(), L"da elaborare");
            }
            if (state->files.empty()) {
                SetWindowTextW(state->status, L"Nessun WAV nella cartella scelta.");
            } else {
                SetWindowTextW(state->status,
                               (L"File trovati: " + std::to_wstring(state->files.size()) +
                                L". Premere Avvia batch.").c_str());
            }
            return 0;
        }
        case WM_SIZE: {
            RECT client = {};
            GetClientRect(window, &client);
            if (state->list) {
                MoveWindow(state->list, 12, 12, client.right - 24, client.bottom - 120, TRUE);
                MoveWindow(state->progress, 12, client.bottom - 100, client.right - 24, 20, TRUE);
                MoveWindow(state->status, 12, client.bottom - 74, client.right - 24, 30, TRUE);
                MoveWindow(state->start, client.right - 220, client.bottom - 36, 110, 28, TRUE);
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kStart) runBatch(state);
            else if (LOWORD(wParam) == kClose) DestroyWindow(window);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            state->done = true;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

void runBatchWindow(HWND parent, const std::string& inputDirectory,
                    const studio::Settings& settings) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = batchProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    BatchState state;
    state.inputDirectory = inputDirectory;
    state.settings = settings;

    RECT owner = {};
    if (parent) GetWindowRect(parent, &owner);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass,
                                  L"NL Breath Click Studio - Batch",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                                  parent ? owner.left + 60 : CW_USEDEFAULT,
                                  parent ? owner.top + 60 : CW_USEDEFAULT,
                                  740, 470, parent, nullptr,
                                  GetModuleHandleW(nullptr), &state);
    if (!window) return;

    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message = {};
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (parent) EnableWindow(parent, TRUE);
}

} // namespace bc

#endif
