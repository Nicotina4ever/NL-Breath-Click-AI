// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  studio_main.cpp — avvio di NL Breath Click Studio
// ============================================================================
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
#include "studio_batch.h"
#include "studio_engine.h"

#ifdef _WIN32

namespace {
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
    SetProcessDPIAware();
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    std::vector<std::string> initialFiles;
    if (arguments) {
        for (int i = 1; i < argumentCount; ++i) {
            const std::string path = toUtf8(arguments[i]);
            if (!path.empty()) initialFiles.push_back(path);
        }
        LocalFree(arguments);
    }

    // Test headless del progetto multi-file: salva, riapre e verifica che le
    // sessioni siano state scritte. Non e' il percorso utente, ma permette a
    // CTest di coprire il formato di progetto senza pilotare un dialogo.
    if (initialFiles.size() >= 4 && initialFiles[0] == "--project-test") {
        bc::EditorWindow window;
        if (!window.create(instance)) return 1;
        window.setStudioMode(true);
        for (size_t i = 1; i + 1 < initialFiles.size(); ++i) window.openInitialFile(initialFiles[i]);
        if (!window.analyzeAllTracks()) return 6;
        const std::string projectPath = initialFiles.back();
        if (!window.saveProjectPath(projectPath)) return 2;
        if (!window.openProjectPath(projectPath)) return 3;
        return 0;
    }

    // Flusso completo headless per la prova automatica: importa, analizza
    // tutti, approva, esporta. Non sostituisce il workflow GUI, ma verifica
    // che le stesse funzioni siano collegate senza dialoghi.
    // Esporta una sessione già revisionata senza aprire dialoghi: serve alla
    // prova di carico con centinaia di eventi automatici inclusi.
    if (initialFiles.size() >= 3 && initialFiles[0] == "--session-export") {
        bc::EditorWindow window;
        if (!window.create(instance)) return 1;
        window.setStudioMode(true);
        if (!window.openSessionFile(initialFiles[1])) return 7;
        const int exported = window.exportAllTo(initialFiles[2], false);
        return exported > 0 ? 0 : 5;
    }

    if (initialFiles.size() >= 3 && initialFiles[0] == "--workflow-test") {
        bc::EditorWindow window;
        if (!window.create(instance)) return 1;
        window.setStudioMode(true);
        const std::string outputDirectory = initialFiles[1];
        for (size_t i = 2; i < initialFiles.size(); ++i) window.openInitialFile(initialFiles[i]);
        if (!window.analyzeAllTracks()) return 4;
        const int exported = window.exportAllTo(outputDirectory, false);
        return exported > 0 ? 0 : 5;
    }

    if (initialFiles.size() >= 2 && initialFiles[0] == "--batch") {
        bc::studio::Settings settings;
        bc::runBatchWindow(nullptr, initialFiles[1], settings);
        return 0;
    }

    bc::EditorWindow window;
    if (!window.create(instance)) {
        MessageBoxW(nullptr, L"Impossibile creare la finestra di NL Breath Click Studio.",
                    L"NL Breath Click Studio", MB_ICONERROR | MB_OK);
        return 1;
    }
    window.setStudioMode(true);
    const bool sessionMode = initialFiles.size() >= 2 && initialFiles[0] == "--session";
    for (size_t i = 0; i < initialFiles.size(); ++i) {
        if (sessionMode) {
            if (i == 1) window.openSessionFile(initialFiles[i]);
            continue;
        }
        window.openInitialFile(initialFiles[i]);
    }
    return window.runMessageLoop();
}

#else
int main() {
    return 1;
}
#endif
