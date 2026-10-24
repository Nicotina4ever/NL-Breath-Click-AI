// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>
#include <filesystem>

namespace bc {
std::filesystem::path nativeFilePath(const std::filesystem::path& path);
bool sameFilePath(const std::string& first, const std::string& second);
// Write beside the target, flush/close, then replace atomically. On failure
// the previous target remains intact; the diagnostic names the retained temp.
bool writeTextAtomic(const std::string& path, const std::string& content,
                     std::string* error = nullptr);
}
