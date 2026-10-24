// SPDX-License-Identifier: MPL-2.0
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/detect/detection.h"
#include "detector_settings_store.h"

namespace {
int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
bool near(float a, float b, float tolerance = 1e-4f) {
    return std::abs(a - b) <= tolerance;
}
} // namespace

int main() {
    bc::DetectorSettings defaults;
    bc::DetectorSettings conservative = defaults;
    bc::DetectorSettings high = defaults;
    bc::applySensitivityPreset(conservative, bc::SensitivityPreset::Conservative);
    bc::applySensitivityPreset(high, bc::SensitivityPreset::High);
    if (!(conservative.breathThreshold > defaults.breathThreshold)) {
        return fail("conservative threshold not higher");
    }
    if (!(high.breathThreshold < defaults.breathThreshold)) {
        return fail("high threshold not lower");
    }
    if (!(high.breathSensitivity > defaults.breathSensitivity)) {
        return fail("high sensitivity not higher");
    }

    bc::DetectorSettings saved = defaults;
    saved.breathThreshold = 0.037f;
    saved.clickThreshold = 0.081f;
    saved.minBreathMs = 123;
    saved.mergeGapMs = 73;
    saved.requirePauseForBreath = false;
    saved.useBreathModel = true;
    saved.detectClicks = false;

    const auto directory = std::filesystem::temp_directory_path() / "bc_settings_store_tests";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const std::string path = (directory / "test.bcpreset").u8string();
    std::string error;
    if (!bc::saveDetectorPreset(path, saved, &error)) {
        std::fprintf(stderr, "save: %s\n", error.c_str());
        return fail("save preset");
    }
    if (!std::filesystem::exists(std::filesystem::u8path(path))) {
        return fail("preset file missing");
    }
    bc::DetectorSettings loaded;
    if (!bc::loadDetectorPreset(path, loaded, &error)) {
        std::fprintf(stderr, "load: %s\n", error.c_str());
        return fail("load preset");
    }
    if (!near(loaded.breathThreshold, 0.037f) ||
        !near(loaded.clickThreshold, 0.081f) ||
        loaded.minBreathMs != 123 ||
        loaded.mergeGapMs != 73 ||
        loaded.requirePauseForBreath ||
        !loaded.useBreathModel ||
        loaded.detectClicks) {
        return fail("preset roundtrip values");
    }

    bc::DetectorSettings bad;
    const std::string badPath = (directory / "broken.bcpreset").u8string();
    { std::FILE* file = std::fopen(badPath.c_str(), "wb"); std::fputs("schema=999\n", file); std::fclose(file); }
    error.clear();
    if (bc::loadDetectorPreset(badPath, bad, &error)) {
        return fail("invalid preset accepted");
    }
    if (error.empty()) return fail("invalid preset has no error");

    { std::FILE* file = std::fopen(badPath.c_str(), "wb");
      std::fputs("schema=1\nbreathThreshold=nan\nfftSize=999999999\n", file); std::fclose(file); }
    bad.breathThreshold = .123f;
    if (bc::loadDetectorPreset(badPath, bad, &error) || bad.breathThreshold != .123f)
        return fail("unsafe preset partially changed settings");

    std::filesystem::remove_all(directory, ec);
    std::puts("SETTINGS STORE TESTS OK: preset reali e roundtrip");
    return 0;
}
