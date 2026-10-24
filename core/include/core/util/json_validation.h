// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>
#include <cstddef>

namespace bc {
// Validate the complete document before the legacy field readers touch state.
// Rejects truncated input, duplicate keys, invalid escapes/numbers and deep nesting.
bool validJsonDocument(const std::string& text);
// Decode a quoted value at position (UTF-16 escapes -> UTF-8). On failure,
// position and output stay unchanged. Persistence strings cannot contain NUL.
bool decodeJsonString(const std::string& text, size_t& position, std::string& output);
}
