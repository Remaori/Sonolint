#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace sonolint
{
// `existing` (null for a new file) with the section of `platform` rewritten from the
// results of runScenarios: every metric except cpu, one object per channel, rounded.
// Without filters the whole section is replaced; with filters only the scenarios that
// match one are, and the rest is kept. A scenario that ended in error keeps what it had.
nlohmann::json updateBaseline (const nlohmann::json& existing, const nlohmann::json& results, const std::string& platform,
                               const std::vector<std::string>& filters, const std::string& sonolintVersion);

// The scenarios of `platform` in a baseline, or null when it has none.
nlohmann::json platformScenarios (const nlohmann::json& baseline, const std::string& platform);

// The text of a baseline, canonical so its diffs stay small: keys in alphabetical order
// at every level, 2 spaces of indentation, LF line endings and a newline at the end.
std::string formatBaseline (const nlohmann::json& baseline);

// Null when the file doesn't exist. Throws Error (config) when it isn't a baseline.
nlohmann::json loadBaseline (const std::filesystem::path& file);

// Throws Error (config) when the file can't be written.
void saveBaseline (const std::filesystem::path& file, const nlohmann::json& baseline);
} // namespace sonolint
