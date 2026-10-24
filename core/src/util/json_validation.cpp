// SPDX-License-Identifier: MPL-2.0
#include "core/util/json_validation.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>

namespace bc {
bool decodeJsonString(const std::string& text, size_t& position, std::string& output) {
    size_t p = position;
    if (p >= text.size() || text[p++] != '"') return false;
    std::string value;
    auto hex4 = [&](unsigned& code) {
        code = 0;
        for (int i = 0; i < 4; ++i) {
            if (p == text.size() || !std::isxdigit(static_cast<unsigned char>(text[p]))) return false;
            const char h = text[p++];
            code = code * 16 + (h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
        }
        return true;
    };
    while (p < text.size()) {
        unsigned char c = text[p++];
        if (c == '"') { output = std::move(value); position = p; return true; }
        if (c < 32) return false;
        if (c != '\\') { value += char(c); continue; }
        if (p == text.size()) return false;
        c = text[p++];
        if (c == 'u') {
            unsigned code = 0;
            if (!hex4(code) || code == 0) return false; // NUL cannot round-trip through file APIs.
            if (code >= 0xd800 && code <= 0xdbff) {
                if (p + 2 > text.size() || text[p++] != '\\' || text[p++] != 'u') return false;
                unsigned low = 0;
                if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
            } else if (code >= 0xdc00 && code <= 0xdfff) return false;
            if (code < 0x80) value += char(code);
            else if (code < 0x800) {
                value += char(0xc0 | (code >> 6)); value += char(0x80 | (code & 0x3f));
            } else if (code < 0x10000) {
                value += char(0xe0 | (code >> 12)); value += char(0x80 | ((code >> 6) & 0x3f));
                value += char(0x80 | (code & 0x3f));
            } else {
                value += char(0xf0 | (code >> 18)); value += char(0x80 | ((code >> 12) & 0x3f));
                value += char(0x80 | ((code >> 6) & 0x3f)); value += char(0x80 | (code & 0x3f));
            }
            continue;
        }
        switch (c) {
            case '"': case '\\': case '/': value += char(c); break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            default: return false;
        }
    }
    return false;
}

namespace {
struct Parser {
    const std::string& text;
    size_t p = 0;
    void ws() { while (p < text.size() && (text[p] == ' ' || text[p] == '\n' || text[p] == '\r' || text[p] == '\t')) ++p; }
    bool take(char c) { ws(); if (p == text.size() || text[p] != c) return false; ++p; return true; }
    bool string(std::string* key = nullptr) {
        ws();
        std::string value;
        if (!decodeJsonString(text, p, value)) return false;
        if (key) *key = std::move(value);
        return true;
    }
    bool value(int depth = 0) {
        ws();
        if (p == text.size() || depth > 64) return false;
        if (text[p] == '"') return string();
        if (take('{')) {
            std::set<std::string> keys;
            if (take('}')) return true;
            do {
                std::string key;
                if (!string(&key) || !keys.insert(key).second || !take(':') || !value(depth + 1)) return false;
                if (take('}')) return true;
            } while (take(','));
            return false;
        }
        if (take('[')) {
            if (take(']')) return true;
            do { if (!value(depth + 1)) return false; if (take(']')) return true; } while (take(','));
            return false;
        }
        for (const char* literal : {"true", "false", "null"}) {
            const std::string word(literal);
            if (text.compare(p, word.size(), word) == 0) { p += word.size(); return true; }
        }
        const size_t start = p;
        if (text[p] == '-') ++p;
        if (p == text.size()) return false;
        auto digit = [&] { return p < text.size() && text[p] >= '0' && text[p] <= '9'; };
        if (text[p] == '0') ++p;
        else { if (!digit()) return false; while (digit()) ++p; }
        if (p < text.size() && text[p] == '.') { ++p; if (!digit()) return false; while (digit()) ++p; }
        if (p < text.size() && (text[p] == 'e' || text[p] == 'E')) {
            ++p; if (p < text.size() && (text[p] == '+' || text[p] == '-')) ++p;
            if (!digit()) return false; while (digit()) ++p;
        }
        return std::isfinite(std::strtod(text.substr(start, p - start).c_str(), nullptr));
    }
};
}
bool validJsonDocument(const std::string& text) {
    Parser parser{text};
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) parser.p = 3;
    if (!parser.value()) return false;
    parser.ws();
    return parser.p == text.size();
}
}
