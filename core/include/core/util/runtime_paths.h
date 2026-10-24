// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <filesystem>
#include <string>
namespace bc {
// Explicit process-local paths allow the suite to share compatible CPU runtimes/models.
std::filesystem::path executableDirectory();
std::filesystem::path environmentPath(const char* name);
std::filesystem::path runtimeDirectory();
std::string libraryFileName(const std::string& base);
using LibraryHandle = void*;
LibraryHandle openSharedLibrary(const std::filesystem::path& path);
void* librarySymbol(LibraryHandle library, const char* name);
void closeSharedLibrary(LibraryHandle library);
}
