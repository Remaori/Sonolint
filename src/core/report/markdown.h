#pragma once

#include "core/report/describe.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sonolint
{
// The limit of report.md. A GitHub comment takes up to 65 536 characters; counting
// bytes keeps it safe, since a character is at least one byte.
constexpr size_t kMarkdownLimit = 60000;

// report.md, for a pull request comment and the job summary: the status, a table of
// what didn't pass, a chart of each frequency response that changed, every scenario
// in a <details> block, and a footer. Past kMarkdownLimit the <details> block is cut
// first, then the charts; the table never is.
std::string formatMarkdown (const nlohmann::json& results, const ReportInfo& info);
} // namespace sonolint
