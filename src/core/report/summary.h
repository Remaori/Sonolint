#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace sonolint
{
// What `sonolint run` prints at the end: a table of the scenarios that didn't pass, one
// line per problem, then the totals. With `colour` the statuses get ANSI colours; the
// caller decides (a terminal, and NO_COLOR not set).
std::string formatSummary (const nlohmann::json& results, bool colour = false);
} // namespace sonolint
