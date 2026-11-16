#include "core/report/summary.h"

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

std::string expected (const json& rule)
{
    if (rule.contains ("approx"))   return show (rule["approx"]) + " within " + show (rule["tol"]);
    if (rule.contains ("min"))      return "at least " + show (rule["min"]);
    if (rule.contains ("max"))      return "at most " + show (rule["max"]);
    if (rule.contains ("between"))  return "from " + show (rule["between"][0]) + " to " + show (rule["between"][1]);

    return show (rule["equals"]);
}
} // namespace

std::string formatSummary (const json& results)
{
    std::string text;

    for (const auto& scenario : results.at ("scenarios"))
    {
        const auto id = scenario.at ("id").get<std::string>();

        if (scenario.contains ("warnings"))
            for (const auto& warning : scenario["warnings"])
                text += "WARN   " + id + ": " + warning.get<std::string>() + "\n";

        if (scenario.at ("status") == "error")
            text += "ERROR  " + id + ": " + scenario.at ("error").get<std::string>() + "\n";

        if (scenario.at ("status") != "assertion_failed")
            continue;

        for (const auto& assertion : scenario.at ("assertions"))
        {
            if (assertion.at ("passed").get<bool>())
                continue;

            const auto& actual = assertion.at ("actual");

            text += "FAIL   " + id + ": " + assertion.at ("metric").get<std::string>()
                  + (actual.is_array() ? " has points outside: expected " : " is " + show (actual) + ", expected ")
                  + expected (assertion.at ("rule")) + "\n";
        }
    }

    const auto& summary = results.at ("summary");
    const auto count = [&] (const char* status) { return summary.at (status).get<int>(); };

    return text + std::to_string (results.at ("scenarios").size()) + " scenarios: " + std::to_string (count ("pass")) + " pass, "
         + std::to_string (count ("assertion_failed")) + " assertion_failed, " + std::to_string (count ("error")) + " error\n";
}
} // namespace sonolint
