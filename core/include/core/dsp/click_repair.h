// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace bc {
enum class ClickRepairStatus { Repaired, Declined, Invalid };
// Half-open, sample-exact repair; reads immutable original context. No duration change.
// Declined leaves every channel intact: auto cannot justify reconstructing voice.
ClickRepairStatus repairClick(std::vector<float>& audio, int channels, double sampleRate,
                 int64_t first, int64_t last, const std::string& method = "auto");
}
