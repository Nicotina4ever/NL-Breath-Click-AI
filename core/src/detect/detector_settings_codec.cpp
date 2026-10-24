// SPDX-License-Identifier: MPL-2.0
#include "core/detect/detector_settings_codec.h"
#include <map>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>

namespace bc {
namespace {
std::map<std::string, std::string> parseLines(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return values;
}

float readFloat(const std::map<std::string, std::string>& values,
                const std::string& key, float fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    try {
        size_t consumed = 0;
        float value = std::stof(it->second, &consumed);
        if (consumed != it->second.size()) return NAN;
        return value;
    } catch (...) { return NAN; }
}

int readInt(const std::map<std::string, std::string>& values,
            const std::string& key, int fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    try {
        size_t consumed = 0;
        int value = std::stoi(it->second, &consumed);
        return consumed == it->second.size() ? value : -1;
    } catch (...) {
        return -1;
    }
}

bool readBool(const std::map<std::string, std::string>& values,
              const std::string& key, bool fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    if (it->second == "1" || it->second == "true") return true;
    if (it->second == "0" || it->second == "false") return false;
    throw std::invalid_argument("booleano non valido");
}

void applyFields(const std::map<std::string, std::string>& values,
                 DetectorSettings& settings) {
    settings.breathThreshold = readFloat(values, "breathThreshold", settings.breathThreshold);
    settings.clickThreshold = readFloat(values, "clickThreshold", settings.clickThreshold);
    settings.breathSensitivity = readFloat(values, "breathSensitivity", settings.breathSensitivity);
    settings.clickSensitivity = readFloat(values, "clickSensitivity", settings.clickSensitivity);
    settings.minBreathMs = readInt(values, "minBreathMs", settings.minBreathMs);
    settings.maxBreathMs = readInt(values, "maxBreathMs", settings.maxBreathMs);
    settings.minClickMs = readInt(values, "minClickMs", settings.minClickMs);
    settings.maxClickMs = readInt(values, "maxClickMs", settings.maxClickMs);
    settings.mergeGapMs = readInt(values, "mergeGapMs", settings.mergeGapMs);
    settings.pauseRadiusMs = readInt(values, "pauseRadiusMs", settings.pauseRadiusMs);
    settings.requirePauseForBreath =
        readBool(values, "requirePauseForBreath", settings.requirePauseForBreath);
    settings.detectBreath = readBool(values, "detectBreath", settings.detectBreath);
    settings.detectClicks = readBool(values, "detectClicks", settings.detectClicks);
    settings.useBreathModel = readBool(values, "useBreathModel", settings.useBreathModel);
    settings.breathModelThreshold =
        readFloat(values, "breathModelThreshold", settings.breathModelThreshold);
    settings.usaReteNeurale = readBool(values, "usaReteNeurale", settings.usaReteNeurale);
    settings.proposteLarghe = readBool(values, "proposteLarghe", settings.proposteLarghe);
    settings.fftSize = readInt(values, "fftSize", settings.fftSize);
    settings.hopSize = readInt(values, "hopSize", settings.hopSize);
}

}

std::string serializeDetectorSettings(const DetectorSettings& settings) {
    std::ostringstream out;
    out << "schema=1\n";
    out << std::setprecision(9);
    out << "breathThreshold=" << settings.breathThreshold << "\n";
    out << "clickThreshold=" << settings.clickThreshold << "\n";
    out << "breathSensitivity=" << settings.breathSensitivity << "\n";
    out << "clickSensitivity=" << settings.clickSensitivity << "\n";
    out << "minBreathMs=" << settings.minBreathMs << "\n";
    out << "maxBreathMs=" << settings.maxBreathMs << "\n";
    out << "minClickMs=" << settings.minClickMs << "\n";
    out << "maxClickMs=" << settings.maxClickMs << "\n";
    out << "mergeGapMs=" << settings.mergeGapMs << "\n";
    out << "pauseRadiusMs=" << settings.pauseRadiusMs << "\n";
    out << "requirePauseForBreath=" << (settings.requirePauseForBreath ? 1 : 0) << "\n";
    out << "detectBreath=" << (settings.detectBreath ? 1 : 0) << "\n";
    out << "detectClicks=" << (settings.detectClicks ? 1 : 0) << "\n";
    out << "useBreathModel=" << (settings.useBreathModel ? 1 : 0) << "\n";
    out << "breathModelThreshold=" << settings.breathModelThreshold << "\n";
    out << "usaReteNeurale=" << (settings.usaReteNeurale ? 1 : 0) << "\n";
    out << "proposteLarghe=" << (settings.proposteLarghe ? 1 : 0) << "\n";
    out << "fftSize=" << settings.fftSize << "\n";
    out << "hopSize=" << settings.hopSize << "\n";
    return out.str();
}

bool parseDetectorSettings(const std::string& text, DetectorSettings& settings, std::string* error) {
    const auto values = parseLines(text);
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "1") {
        if (error) *error = "preset non supportato: schema mancante o diverso da 1";
        return false;
    }
    auto candidate = settings;
    try { applyFields(values, candidate); }
    catch (...) { if (error) *error = "preset: campo booleano non valido"; return false; }
    auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (!unit(candidate.breathThreshold) || !unit(candidate.clickThreshold) ||
        !unit(candidate.breathSensitivity) || !unit(candidate.clickSensitivity) ||
        !unit(candidate.breathModelThreshold) || candidate.fftSize < 128 || candidate.fftSize > 32768 ||
        candidate.hopSize < 16 || candidate.hopSize > candidate.fftSize ||
        candidate.minBreathMs < 1 || candidate.maxBreathMs < candidate.minBreathMs ||
        candidate.maxBreathMs > 60000 || candidate.minClickMs < 1 ||
        candidate.maxClickMs < candidate.minClickMs || candidate.maxClickMs > 1000 ||
        candidate.mergeGapMs < 0 || candidate.mergeGapMs > 60000 ||
        candidate.pauseRadiusMs < 0 || candidate.pauseRadiusMs > 60000) {
        if (error) *error = "impostazioni non valide: valori non finiti o fuori intervallo";
        return false;
    }
    settings = candidate;
    if (error) error->clear();
    return true;
}
}
