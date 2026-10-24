// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "core/detect/detection.h"

namespace bc {
// Shared representation for global defaults, presets, projects and sessions.
std::string serializeDetectorSettings(const DetectorSettings& settings);
bool parseDetectorSettings(const std::string& text, DetectorSettings& settings,
                           std::string* error = nullptr);
}
