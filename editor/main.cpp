// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  main.cpp - Avvio dell'editor
//
//  Uso:  bc_editor [file.wav]
// ============================================================================
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "editor_window.h"

#ifdef _WIN32

namespace {

// Il percorso arriva come UTF-16 dalla riga di comando: convertirlo in UTF-8
// mantiene la coerenza con il resto del motore, che lavora in UTF-8.
std::string toUtf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string result(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), needed, nullptr, nullptr);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Un'applicazione con finestre deve dichiarare di essere consapevole dei
    // DPI, altrimenti su schermi ad alta densita' Windows la scala e i
    // controlli risultano sfocati.
    SetProcessDPIAware();

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);

    // Si accettano PIU' file: ognuno diventa una traccia. E' comodo per
    // aprire in un colpo solo i take della stessa battuta, ed e' anche
    // l'unico modo per provare la multi-traccia in automatico (il dialogo
    // di apertura non e' pilotabile da un test).
    std::vector<std::string> initialFiles;
    if (arguments) {
        for (int i = 1; i < argumentCount; ++i) {
            const std::string percorso = toUtf8(arguments[i]);
            if (!percorso.empty()) {
                initialFiles.push_back(percorso);
            }
        }
        LocalFree(arguments);
    }

    bc::EditorWindow window;
    if (!window.create(instance)) {
        MessageBoxW(nullptr, L"Impossibile creare la finestra dell'editor.",
                    L"BreathClick Editor", MB_ICONERROR | MB_OK);
        return 1;
    }

    for (const std::string& percorso : initialFiles) {
        window.openInitialFile(percorso);
    }

    return window.runMessageLoop();
}

#else

int main() {
    std::cerr << "L'editor e' disponibile solo su Windows.\n";
    return 1;
}

#endif
