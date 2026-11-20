#include "core/report/markdown.h"

#include "core/compare/compare.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

// Text that can go in a table cell: no "|" to end it early, no line breaks, no HTML.
std::string cell (const std::string& text)
{
    std::string escaped;

    for (const auto c : text)
    {
        if (c == '|')                    escaped += "\\|";
        else if (c == '<')               escaped += "&lt;";
        else if (c == '\n' || c == '\r') escaped += ' ';
        else                             escaped += c;
    }

    return escaped;
}

std::string statusLine (const json& summary)
{
    std::string line;

    for (const auto& status : statusesWorstFirst())
        if (const auto count = summary.value (status, 0); count > 0)
            line += (line.empty() ? "" : " · ") + std::to_string (count) + " " + statusLabel (status);

    return "**" + line + "**";
}


std::string frequencyLabel (double hz) { return hz < 1000.0 ? showValue (hz) : showValue (hz / 1000.0) + "k"; }

// A Mermaid chart of a frequency response that changed. Mermaid draws no legend, so
// the text before it says which line is which.
std::string responseChart (const std::string& id, const ChannelCurve& curve, const json& freqs)
{
    const auto& actual = curve.actual;
    const auto& baseline = curve.baseline;

    if (! actual.is_array() || ! baseline.is_array() || actual.size() != baseline.size() || freqs.size() != actual.size() || actual.empty())
        return {};   // a different number of points can't share one axis

    // Mermaid writes a label under every point, and past about 12 they run into each
    // other. Then it gets about 10, evenly spaced, plus the point that moved the most.
    const auto count = actual.size();
    std::vector<size_t> shown;
    size_t most = 0;

    for (size_t i = 0; i < count; ++i)
        if (std::abs (actual[i].get<double>() - baseline[i].get<double>()) > std::abs (actual[most].get<double>() - baseline[most].get<double>()))
            most = i;

    for (size_t i = 0; i < count; i += count > 12 ? (count + 9) / 10 : 1)
        shown.push_back (i);

    if (shown.back() != count - 1)
        shown.push_back (count - 1);

    if (std::find (shown.begin(), shown.end(), most) == shown.end())
        shown.insert (std::upper_bound (shown.begin(), shown.end(), most), most);

    double low = 0.0, high = 0.0;
    std::string labels, actualPoints, baselinePoints;

    for (const auto i : shown)
    {
        const auto separator = labels.empty() ? "" : ", ";
        labels += separator + ("\"" + frequencyLabel (freqs[i].get<double>()) + "\"");
        actualPoints += separator + showValue (actual[i]);
        baselinePoints += separator + showValue (baseline[i]);
        low = std::min ({ low, actual[i].get<double>(), baseline[i].get<double>() });
        high = std::max ({ high, actual[i].get<double>(), baseline[i].get<double>() });
    }

    const auto partial = shown.size() < count ? " " + std::to_string (shown.size()) + " of its " + std::to_string (count) + " points; all of them are in the HTML report." : "";

    return "**Frequency response of " + id + ", " + showChannels (curve.channels) + ".** First line: this run. Second line: the baseline." + partial + "\n\n"
           "```mermaid\n"
           "xychart-beta\n"
           "    x-axis \"Frequency (Hz)\" [" + labels + "]\n"
           "    y-axis \"Magnitude (dB)\" " + showValue (std::floor (low - 1.0)) + " --> " + showValue (std::ceil (high + 1.0)) + "\n"
           "    line [" + actualPoints + "]\n"
           "    line [" + baselinePoints + "]\n"
           "```\n";
}

// What didn't pass in a scenario, for its row in the <details> block.
std::string notPassing (const json& scenario)
{
    std::vector<std::string> names;

    for (const auto& problem : problemsOf (scenario))
    {
        const auto name = problem.metric.empty() ? problem.text : problem.metric.substr (0, problem.metric.find (" ("));

        if (std::find (names.begin(), names.end(), name) == names.end())
            names.push_back (name);
    }

    std::string text;

    for (const auto& name : names)
        text += (text.empty() ? "" : ", ") + name;

    return cell (text);
}
} // namespace

std::string formatMarkdown (const json& results, const ReportInfo& info)
{
    const auto& plugin = results.at ("plugin");
    const auto& scenarios = results.at ("scenarios");

    // The platform too: a matrix of runners puts one comment per platform on the pull request.
    const auto platform = results.contains ("environment") ? results["environment"].value ("platform", "") : std::string();
    auto head = "<!-- sonolint:report -->\n## Sonolint · "
              + cell (plugin.at ("name").get<std::string>() + " " + plugin.at ("version").get<std::string>() + (platform.empty() ? "" : " · " + platform))
              + "\n\n" + statusLine (results.at ("summary")) + "\n\n";

    // Four columns, like the HTML report: more would overflow the width of a comment on GitHub.
    std::string table;

    for (const auto& row : allProblems (results))
        table += "| " + cell (row.scenarios) + " | " + cell (row.problem.metric) + " | " + cell (row.problem.text) + " | " + statusLabel (row.problem.status) + " |\n";

    head += table.empty() ? "No regressions, changes, failed rules or errors.\n"
                          : "| Scenario | Metric | What happened | Status |\n"
                            "| --- | --- | --- | --- |\n" + table;

    std::vector<std::string> charts;

    for (const auto& scenario : scenarios)
    {
        const auto freqs = scenario.contains ("metrics") && scenario["metrics"].contains ("frequency_response")
                               ? scenario["metrics"]["frequency_response"]["per_channel"][0].value ("freqs_hz", json())
                               : json();

        for (const auto& curve : curvesOf (scenario, "frequency_response", "magnitude_db"))
            if (curve.status == "changed" || curve.status == "regressed")
                if (const auto chart = responseChart (scenario.at ("id").get<std::string>(), curve, freqs); ! chart.empty())
                    charts.push_back (chart);
    }

    std::vector<std::string> rows;

    for (const auto& scenario : scenarios)
        rows.push_back ("| " + scenario.at ("id").get<std::string>() + " | " + statusLabel (scenario.at ("status").get<std::string>()) + " | " + notPassing (scenario) + " |\n");

    std::string footer = "---\n";

    for (const auto& part : { info.artifactUrl.empty() ? std::string() : "[Full report](" + info.artifactUrl + ")",
                              results.contains ("sonolint_version") ? "Sonolint " + results["sonolint_version"].get<std::string>() : std::string(),
                              showCommit (info) })
        if (! part.empty())
            footer += (footer.size() > 4 ? " · " : "") + part;

    footer += "\n";

    // Put together, leaving out rows of the <details> block and then charts until it fits.
    const auto assemble = [&] (size_t numCharts, size_t numRows)
    {
        auto text = head;

        for (size_t i = 0; i < numCharts; ++i)
            text += "\n" + charts[i];

        text += "\n<details>\n<summary>All scenarios (" + std::to_string (scenarios.size()) + ")</summary>\n\n"
                "| Scenario | Status | Not passing |\n| --- | --- | --- |\n";

        for (size_t i = 0; i < numRows; ++i)
            text += rows[i];

        if (numRows < rows.size())
            text += "\n*Truncated, see the HTML report.*\n";

        return text + "\n</details>\n\n" + footer;
    };

    auto numCharts = charts.size(), numRows = rows.size();
    auto text = assemble (numCharts, numRows);

    while (text.size() > kMarkdownLimit && (numRows > 0 || numCharts > 0))
    {
        if (numRows > 0)
            --numRows;
        else
            --numCharts;

        text = assemble (numCharts, numRows);
    }

    return text;
}
} // namespace sonolint
