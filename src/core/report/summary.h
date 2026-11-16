#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace sonolint
{
// What `sonolint run` prints at the end: one line for each scenario that didn't pass
// and why, then the totals. `results` is the document that runScenarios returns.
std::string formatSummary (const nlohmann::json& results);
} // namespace sonolint
