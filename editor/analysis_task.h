// SPDX-License-Identifier: MPL-2.0
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "studio_engine.h"

namespace bc {
// Modal interaction, background computation. The owner must reject mutations
// while this function pumps messages. No document/HWND enters the worker.
bool analyzeWithProgress(HWND owner, const std::string& path,
                         const studio::Settings& settings, studio::Analysis* result,
                         std::string* error, studio::AnalysisControl& control);
}
#endif
