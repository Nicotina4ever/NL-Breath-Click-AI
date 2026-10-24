// SPDX-License-Identifier: MPL-2.0
#include "core/util/logger.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <climits>
#include <sys/stat.h>
#include <sstream>

#ifdef _WIN32
    #include <io.h>
    #include <direct.h>
    #define PATH_SEP '\\'
#else
    #define PATH_SEP '/'
#endif

Logger::Logger(const std::string& filename)
    : file_(nullptr), filename_(filename)
{
    bool isAbsolute = false;

#ifdef _WIN32
    if (filename.size() > 2 && (filename[0] >= 'a' && filename[0] <= 'z' || filename[0] >= 'A' && filename[0] <= 'Z') && filename[1] == ':') {
        isAbsolute = true;
    }
#else
    if (!filename.empty() && filename[0] == '/') {
        isAbsolute = true;
    }
#endif

    std::string logPath;
    if (isAbsolute) {
        logPath = filename;
    } else {
        logPath = "logs/" + filename;
    }

    // Ensure logs directory exists
#ifdef _WIN32
    _mkdir("logs");
    FILE* f = fopen(logPath.c_str(), "a");
#else
    mkdir("logs", 0755);
    FILE* f = fopen(logPath.c_str(), "a");
#endif

    if (!f) {
        fprintf(stderr, "Logger: Failed to open log file: %s\n", logPath.c_str());
        file_ = nullptr;
        return;
    }

    file_ = f;
    filename_ = logPath;
}

Logger::~Logger()
{
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
}

std::string Logger::getCurrentTimestamp()
{
    std::time_t now = std::time(nullptr);
    struct tm timeInfo;

#ifdef _WIN32
    localtime_s(&timeInfo, &now);
#else
    localtime_r(&now, &timeInfo);
#endif

    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return std::string(buf);
}

void Logger::writeMessage(const std::string& level, const std::string& msg)
{
    if (!file_) return;

    std::string timestamp = getCurrentTimestamp();
    fprintf(file_, "[%s] [%s] %s\n", timestamp.c_str(), level.c_str(), msg.c_str());
    fflush(file_);
}

void Logger::info(const std::string& msg)
{
    writeMessage("INFO", msg);
}

void Logger::warning(const std::string& msg)
{
    writeMessage("WARNING", msg);
}

void Logger::error(const std::string& msg)
{
    writeMessage("ERROR", msg);
}
