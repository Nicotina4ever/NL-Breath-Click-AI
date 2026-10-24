// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bc {

#ifdef _WIN32
void showAboutDialog(HWND parent);
bool showAttenuationDialog(HWND parent, float& reductionDb, double& fadeMs,
                           double* crossfadeMs = nullptr);
bool showKeepFractionDialog(HWND parent, double& keepFraction);
bool chooseDirectory(HWND parent, const std::wstring& title, std::string& out);
bool chooseFilePath(HWND parent, bool save, const wchar_t* filter,
                    const wchar_t* defaultExtension, std::string& out);
#endif

} // namespace bc
