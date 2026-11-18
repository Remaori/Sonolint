#include "core/report/summary.h"

#include "core/compare/compare.h"
#include "core/report/describe.h"

#include <algorithm>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

std::string padded (const std::string& text, size_t width) { return text + std::string (width - std::min (width, text.size()), ' '); }

std::string painted (const std::string& text, const std::string& status)
{
    const auto* code = status == "improved"                                        ? "\x1b[32m"    // green
                     : status == "removed" || status == "pass" || status == "new"  ? "\x1b[33m"    // yellow: only a warning brings these here
                                                                                   : "\x1b[31m";   // red
    return code + text + "\x1b[0m";
}
} // namespace

std::string formatSummary (const json& results, bool colour)
{
    struct Row
    {
        std::string id, status;
        std::vector<std::string> lines;
    };

    std::vector<Row> rows;

    for (const auto& scenario : results.at ("scenarios"))
    {
        Row row { scenario.at ("id").get<std::string>(), scenario.at ("status").get<std::string>(), {} };

        for (const auto& warning : scenario.value ("warnings", json::array()))
            row.lines.push_back ("warning: " + warning.get<std::string>());

        for (const auto& problem : problemsOf (scenario))
            row.lines.push_back (problem.metric.empty() ? problem.text : problem.metric + ": " + problem.text);

        if (! row.lines.empty())
            rows.push_back (std::move (row));
    }

    std::string text;

    if (! rows.empty())
    {
        size_t idWidth = 8, statusWidth = 6;   // "SCENARIO", "STATUS"

        for (const auto& row : rows)
        {
            idWidth = std::max (idWidth, row.id.size());
            statusWidth = std::max (statusWidth, row.status.size());
        }

        text += padded ("SCENARIO", idWidth) + "  " + padded ("STATUS", statusWidth) + "  WHAT\n";

        for (const auto& row : rows)
        {
            for (size_t i = 0; i < row.lines.size(); ++i)
            {
                const auto status = padded (i == 0 ? row.status : "", statusWidth);
                text += padded (i == 0 ? row.id : "", idWidth) + "  " + (colour && i == 0 ? painted (status, row.status) : status) + "  "
                      + row.lines[i] + "\n";
            }
        }
    }

    // The totals, worst first, leaving out the statuses no scenario has.
    const auto& summary = results.at ("summary");
    std::string totals;

    for (const auto& status : statusesWorstFirst())
        if (const auto count = summary.value (status, 0); count > 0)
            totals += (totals.empty() ? "" : ", ") + std::to_string (count) + " " + status;

    text += std::to_string (results.at ("scenarios").size()) + " scenarios: " + totals + "\n";

    if (summary.value ("improved", 0) + summary.value ("removed", 0) > 0)
        text += "Improved and removed values never fail; to accept them, run sonolint baseline update.\n";

    return text;
}
} // namespace sonolint
