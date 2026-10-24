// SPDX-License-Identifier: MPL-2.0
#include "detector_settings_store.h"
#include "core/detect/detector_settings_codec.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <cmath>
#include "core/util/atomic_file.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bc {
namespace {

std::string executableDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    std::wstring full(path, length);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    const std::wstring directory = full.substr(0, slash);
    const int needed = WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string result(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1, result.data(), needed,
                        nullptr, nullptr);
    return result;
#else
    return {};
#endif
}


} // namespace

const char* sensitivityPresetName(SensitivityPreset preset) {
    switch (preset) {
        case SensitivityPreset::Conservative: return "Conservativo";
        case SensitivityPreset::High: return "Alta sensibilità";
        default: return "Bilanciato";
    }
}

void applySensitivityPreset(DetectorSettings& settings, SensitivityPreset preset) {
    // Questi sono parametri reali del rilevatore, non etichette pubblicitarie.
    settings.requirePauseForBreath = true;
    settings.minBreathMs = 90;
    settings.maxBreathMs = 900;
    settings.mergeGapMs = 60;
    settings.pauseRadiusMs = 200;
    settings.breathThreshold = 0.02f;
    settings.breathSensitivity = 0.80f;
    settings.clickSensitivity = 0.50f;
    switch (preset) {
        case SensitivityPreset::Conservative:
            settings.breathThreshold = 0.035f;
            settings.breathSensitivity = 0.65f;
            settings.minBreathMs = 100;
            settings.mergeGapMs = 50;
            break;
        case SensitivityPreset::High:
            settings.breathThreshold = 0.012f;
            settings.breathSensitivity = 0.92f;
            settings.minBreathMs = 80;
            settings.mergeGapMs = 75;
            break;
        case SensitivityPreset::Balanced:
        default:
            break;
    }
}

std::string detectorSettingsDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH) > 0 && path[0] != 0) {
        std::wstring directory = std::wstring(path) + L"\\BreathClickStudio";
        const int needed = WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1,
                                               nullptr, 0, nullptr, nullptr);
        if (needed > 1) {
            std::string result(static_cast<size_t>(needed - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, directory.c_str(), -1, result.data(), needed,
                                nullptr, nullptr);
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::u8path(result), ec);
            return result;
        }
    }
#endif
    const std::string base = executableDirectory();
    if (base.empty()) return ".";
    std::error_code ec;
    const auto directory = std::filesystem::u8path(base) / "BreathClickSettings";
    std::filesystem::create_directories(directory, ec);
    return directory.u8string();
}

bool loadDetectorSettings(DetectorSettings& settings, std::string* error) {
    const auto path = std::filesystem::u8path(detectorSettingsDirectory()) /
                      "detector_settings.ini";
    std::ifstream input(nativeFilePath(path), std::ios::binary);
    if (!input) {
        if (error) error->clear();
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseDetectorSettings(buffer.str(), settings, error);
}

bool saveDetectorSettings(const DetectorSettings& settings, std::string* error) {
    const auto directory = std::filesystem::u8path(detectorSettingsDirectory());
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    return writeTextAtomic((directory / "detector_settings.ini").u8string(),
        "# BreathClick Studio - impostazioni di rilevamento confermate\n" + serializeDetectorSettings(settings), error);
}

bool saveDetectorPreset(const std::string& path, const DetectorSettings& settings,
                        std::string* error) {
    return writeTextAtomic(path, "# BreathClick Studio detector preset\n" + serializeDetectorSettings(settings), error);
}

bool loadDetectorPreset(const std::string& path, DetectorSettings& settings,
                        std::string* error) {
    std::ifstream input(nativeFilePath(std::filesystem::u8path(path)), std::ios::binary);
    if (!input) {
        if (error) *error = "impossibile aprire il preset";
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseDetectorSettings(buffer.str(), settings, error);
}

std::vector<std::string> listDetectorPresets() {
    std::vector<std::string> presets;
    std::error_code ec;
    const auto directory = std::filesystem::u8path(detectorSettingsDirectory());
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        if (entry.path().extension() == ".bcpreset") {
            presets.push_back(entry.path().u8string());
        }
    }
    std::sort(presets.begin(), presets.end());
    return presets;
}

} // namespace bc
