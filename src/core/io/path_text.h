#pragma once

#include <filesystem>
#include <string>

namespace sonolint
{
// A path as UTF-8 text, for messages. path.string() would use the ANSI code page on
// Windows and fail on characters outside it.
inline std::string toUtf8 (const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return { text.begin(), text.end() };
}
} // namespace sonolint
