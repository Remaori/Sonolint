#include "core/report/junit.h"

#include "core/report/describe.h"

#include <vector>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

std::string xml (const std::string& text)
{
    std::string escaped;

    for (const auto c : text)
    {
        switch (c)
        {
            case '&':   escaped += "&amp;"; break;
            case '<':   escaped += "&lt;"; break;
            case '>':   escaped += "&gt;"; break;
            case '"':   escaped += "&quot;"; break;
            case '\'':  escaped += "&apos;"; break;
            default:    escaped += c; break;
        }
    }

    return escaped;
}

struct TestCase
{
    std::string name;
    std::string outcome;   // empty when it passed, or "failure", "error", "skipped"
    std::string message;
};

std::vector<TestCase> casesOf (const json& scenario)
{
    std::vector<TestCase> cases;
    const auto status = scenario.at ("status").get<std::string>();

    if (status == "error")
        cases.push_back ({ "render", "error", scenario.at ("error").get<std::string>() });

    if (status == "removed")
        cases.push_back ({ "scenario", "skipped", "in the baseline, but no longer in the config" });

    for (const auto& assertion : scenario.value ("assertions", json::array()))
    {
        const auto& actual = assertion.at ("actual");
        const auto failed = ! assertion.at ("passed").get<bool>();

        cases.push_back ({ "expect " + assertion.at ("metric").get<std::string>(), failed ? "failure" : "",
                           failed ? (actual.is_array() ? "has points outside" : "is " + showValue (actual)) + ", expected " + showRule (assertion.at ("rule"))
                                  : "" });
    }

    for (const auto& entry : scenario.value ("comparison", json::array()))
    {
        const auto moved = entry.at ("status").get<std::string>();
        TestCase test { entry.at ("metric").get<std::string>() + " (channel " + std::to_string (entry.at ("channel").get<int>()) + ")", "", "" };

        if ((moved == "regressed" || moved == "changed") && ! entry.value ("report_only", false))
            test = { test.name, "failure", moved + ": " + showMovement (entry) };
        else if (moved == "removed")
            test = { test.name, "skipped", "in the baseline, but no longer measured" };

        cases.push_back (std::move (test));
    }

    if (cases.empty())
        cases.push_back ({ "measured", "", "" });   // a suite needs a case; nothing was checked here

    return cases;
}

std::string counts (int tests, int failures, int errors, int skipped)
{
    return "tests=\"" + std::to_string (tests) + "\" failures=\"" + std::to_string (failures) + "\" errors=\"" + std::to_string (errors) + "\" skipped=\""
         + std::to_string (skipped) + "\"";
}
} // namespace

std::string formatJunit (const json& results)
{
    std::string suites;
    int tests = 0, failures = 0, errors = 0, skipped = 0;

    for (const auto& scenario : results.at ("scenarios"))
    {
        const auto id = xml (scenario.at ("id").get<std::string>());
        const auto cases = casesOf (scenario);
        int suiteFailures = 0, suiteErrors = 0, suiteSkipped = 0;
        std::string body;

        for (const auto& test : cases)
        {
            suiteFailures += test.outcome == "failure" ? 1 : 0;
            suiteErrors += test.outcome == "error" ? 1 : 0;
            suiteSkipped += test.outcome == "skipped" ? 1 : 0;

            body += "    <testcase classname=\"" + id + "\" name=\"" + xml (test.name) + "\"";
            body += test.outcome.empty() ? "/>\n" : ">\n      <" + test.outcome + " message=\"" + xml (test.message) + "\"/>\n    </testcase>\n";
        }

        suites += "  <testsuite name=\"" + id + "\" " + counts ((int) cases.size(), suiteFailures, suiteErrors, suiteSkipped) + ">\n" + body + "  </testsuite>\n";
        tests += (int) cases.size();
        failures += suiteFailures;
        errors += suiteErrors;
        skipped += suiteSkipped;
    }

    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites name=\"Sonolint\" " + counts (tests, failures, errors, skipped) + ">\n" + suites
         + "</testsuites>\n";
}
} // namespace sonolint
