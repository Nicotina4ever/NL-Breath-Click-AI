// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>
#include <cstdio>

class Logger {
public:
    explicit Logger(const std::string& filename);
    ~Logger();

    void info(const std::string& msg);
    void warning(const std::string& msg);
    void error(const std::string& msg);

private:
    FILE* file_{ nullptr };
    std::string filename_;

    void writeMessage(const std::string& level, const std::string& msg);
    static std::string getCurrentTimestamp();
};
