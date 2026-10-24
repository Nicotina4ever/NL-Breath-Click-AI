// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

#include "studio_engine.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bc {
#ifdef _WIN32
void runBatchWindow(HWND parent, const std::string& inputDirectory,
                    const studio::Settings& settings);
#endif
} // namespace bc
