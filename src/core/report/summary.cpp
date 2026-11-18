#include "core/report/summary.h"

#include "core/compare/compare.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

std::string show (const json& value)
{
    if (! value.is_number_float())
        return value.dump();

    std::ostringstream out;
    out.imbue (std::locale::classic());
    out << std::setprecision (6) << value.get<double>();
    return out.str();
}

std::string signedText (double value) { return (value > 0.0 ? "+" : "") + show (value); }

// The word at the start of a line, padded so the ids line up.
std::string label (const std::string& word) { return word + std::string (10 - word.size(), ' '); }

std::string expected (const json& rule)
{
    if (rule.contains ("approx"))   return show (rule["approx"]) + " within " + show (rule["tol"]);
    if (rule.contains ("min"))      return "at least " + show (rule["min"]);
    if (rule.contains ("max"))      return "at most " + show (rule["max"]);
    if (rule.contains ("between"))  return "from " + show (rule["between"][0]) + " to " + show (rule["between"][1]);

    return show (rule["equals"]);
}

// A value that moved beyond its tolerance: what it is, what it was and by how much.
std::string movement (const json& entry)
{
    const auto& actual = entry.at ("actual");
    const auto& baseline = entry.at ("baseline");
    auto text = entry.at ("metric").get<std::string>() + " on channel " + std::to_string (entry.at ("channel").get<int>());

    if (actual.is_array() && baseline.is_array() && actual.size() != baseline.size())
        return text + " has " + std::to_string (actual.size()) + " points and the baseline " + std::to_string (baseline.size());

    if (actual.is_array())
        text += " moved at " + std::to_string (entry.at ("points_outside").get<int>()) + " of " + std::to_string (actual.size())
              + " points, the most by " + signedText (entry.at ("delta").get<double>());
    else
        text += " is " + show (actual) + ", was " + show (baseline)
              + (entry.contains ("delta") ? " (" + signedText (entry["delta"].get<double>()) + ")" : "");

    if (! entry.contains ("delta"))
        return text;   // yes/no values have no tolerance

    const auto& tolerance = entry.at ("tolerance");

    return text + "; tolerance " + show (tolerance.at ("abs"))
         + (tolerance.at ("rel").get<double>() > 0.0 ? " + " + show (100.0 * tolerance.at ("rel").get<double>()) + " % of the baseline" : "");
}
} // namespace

std::string formatSummary (const json& results)
{
    std::string text;

    for (const auto& scenario : results.at ("scenarios"))
    {
        const auto id = scenario.at ("id").get<std::string>();
        const auto status = scenario.at ("status").get<std::string>();

        for (const auto& warning : scenario.value ("warnings", json::array()))
            text += label ("WARN") + id + ": " + warning.get<std::string>() + "\n";

        if (status == "error")
            text += label ("ERROR") + id + ": " + scenario.at ("error").get<std::string>() + "\n";

        if (status == "removed")
            text += label ("REMOVED") + id + ": in the baseline, but no longer in the config\n";

        for (const auto& assertion : scenario.value ("assertions", json::array()))
        {
            if (assertion.at ("passed").get<bool>())
                continue;

            const auto& actual = assertion.at ("actual");

            text += label ("FAIL") + id + ": " + assertion.at ("metric").get<std::string>()
                  + (actual.is_array() ? " has points outside: expected " : " is " + show (actual) + ", expected ")
                  + expected (assertion.at ("rule")) + "\n";
        }

        for (const auto& entry : scenario.value ("comparison", json::array()))
        {
            const auto moved = entry.at ("status").get<std::string>();

            if (moved != "regressed" && moved != "changed" && moved != "improved")
                continue;

            std::string word = moved;
            std::transform (word.begin(), word.end(), word.begin(), [] (char c) { return (char) std::toupper (c); });
            text += label (entry.value ("report_only", false) ? "INFO" : word) + id + ": " + movement (entry) + "\n";
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
