// SPDX-License-Identifier: MPL-2.0
#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace bc {
// Keep command arguments UTF-8 throughout the engines, independent of the
// Windows ANSI code page. Does not change the process/system locale.
inline int runWithUtf8Arguments(int argc, wchar_t** wide, int (*entry)(int, char**)) {
    std::vector<std::string> text(static_cast<size_t>(argc));
    std::vector<char*> argv(static_cast<size_t>(argc) + 1, nullptr);
    for (int i = 0; i < argc; ++i) {
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1,
                                               nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) return 2;
        text[i].resize(static_cast<size_t>(bytes));
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1,
                                 text[i].data(), bytes, nullptr, nullptr)) return 2;
        text[i].pop_back();
        argv[i] = text[i].data();
    }
    return entry(argc, argv.data());
}
} // namespace bc
#endif
