// SPDX-License-Identifier: MPL-2.0
#include "core/util/atomic_file.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif

namespace bc {
std::filesystem::path nativeFilePath(const std::filesystem::path& path) {
#ifdef _WIN32
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    if (ec) return path;
    const auto text = absolute.lexically_normal().wstring();
    if (text.rfind(L"\\\\?\\", 0) == 0) return absolute;
    if (text.rfind(L"\\\\", 0) == 0) return std::filesystem::path(L"\\\\?\\UNC\\" + text.substr(2));
    return std::filesystem::path(L"\\\\?\\" + text);
#else
    return path;
#endif
}

bool sameFilePath(const std::string& first, const std::string& second) {
    if (first.empty() || second.empty()) return false;
    std::error_code ec;
    const auto a = nativeFilePath(std::filesystem::u8path(first));
    const auto b = nativeFilePath(std::filesystem::u8path(second));
    if (std::filesystem::equivalent(a, b, ec) && !ec) return true;
    auto left = std::filesystem::weakly_canonical(a, ec).generic_u8string();
    if (ec) return false;
    auto right = std::filesystem::weakly_canonical(b, ec).generic_u8string();
    if (ec) return false;
#ifdef _WIN32
    auto lower = [](unsigned char c) { return char(std::tolower(c)); };
    std::transform(left.begin(), left.end(), left.begin(), lower);
    std::transform(right.begin(), right.end(), right.begin(), lower);
#endif
    return left == right;
}

bool writeTextAtomic(const std::string& path, const std::string& content, std::string* error) {
    static std::atomic<unsigned long long> serial{0};
    if (path.empty()) { if (error) *error = "percorso di salvataggio vuoto"; return false; }
    const auto target = nativeFilePath(std::filesystem::u8path(path));
    const auto temp = target.parent_path() / (".bc_tmp_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "_" + std::to_string(++serial));
    auto failed = [&](const std::string& why) {
        if (error) *error = why + ": " + target.u8string() + " (temporaneo: " + temp.u8string() + ")";
        return false;
    };
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return failed("Impossibile creare il file temporaneo");
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) return failed("Scrittura incompleta");
        out.close();
        if (!out) return failed("Chiusura del file non riuscita");
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return failed("Sostituzione atomica non riuscita (" + std::to_string(GetLastError()) + ")");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) return failed("Sostituzione atomica non riuscita: " + ec.message());
#endif
    if (error) error->clear();
    return true;
}
}
