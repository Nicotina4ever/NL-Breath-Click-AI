// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace bc {
enum class ClickRepairStatus { Repaired, Declined, Invalid, Bypassed };
struct ClickRepairDiagnostics {
    std::string requestedMethod, selectedMethod = "none", reason = "invalid_input";
    double durationMs = 0, contextMs = 0;
    double linearValidationError = -1, predictiveValidationError = -1;
    double linearVoiceRisk = -1;
    double predictiveBridgeError = -1, predictiveVoiceRisk = -1;
    double predictionDisagreement = -1, impulseRatio = -1;
    double contextHighBandRatio = -1, contextLevelRatio = -1;
    double boundaryLevelRatio = -1;
};
// Half-open, sample-exact repair; reads immutable original context. No duration change.
// Declined leaves every channel intact, including unsafe manual reconstructions.
ClickRepairStatus repairClick(std::vector<float>& audio, int channels, double sampleRate,
                 int64_t first, int64_t last, const std::string& method = "auto",
                 ClickRepairDiagnostics* diagnostics = nullptr);
}
