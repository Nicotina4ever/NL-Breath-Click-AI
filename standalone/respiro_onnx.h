// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bc {
namespace studio {

struct AiEvent {
    int64_t startFrame = 0;  // frame da 10 ms a 16 kHz
    int64_t endFrame = 0;    // esclusivo
    float score = 0.0f;
};

bool respiroEnAvailable(const std::string& modelPath, std::string* resolvedPath,
                        std::string* error);
bool respiroEnDetect(const std::vector<float>& mono,
                     int sampleRate,
                     const std::string& modelPath,
                     float threshold,
                     std::vector<AiEvent>* events,
                     std::vector<float>* probabilities,
                     std::string* error);

} // namespace studio
} // namespace bc
