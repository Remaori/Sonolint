#include "core/report/markdown.h"
#include "report_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

TEST_CASE ("report.md is byte for byte the golden one")
{
    std::ifstream golden (std::filesystem::path (SONOLINT_GOLDEN) / "report.md", std::ios::binary);
    REQUIRE (golden);
    std::ostringstream expected;
    expected << golden.rdbuf();

    CHECK (sonolint::formatMarkdown (test::reportResults(), test::reportInfo()) == expected.str());
}

TEST_CASE ("report.md starts with the marker the Action looks for, and has a footer without the optional parts too")
{
    const auto text = sonolint::formatMarkdown (test::reportResults(), {});

    CHECK (text.rfind ("<!-- sonolint:report -->\n", 0) == 0);
    CHECK (text.find ("[Full report]") == std::string::npos);
    CHECK (text.find ("Commit") == std::string::npos);
    CHECK (text.find ("---\nSonolint 0.1.0\n") != std::string::npos);

    // A commit that isn't a full SHA is shown as it is.
    CHECK (sonolint::showCommit ({ "", "v1.2", "" }) == "Commit v1.2");
}

TEST_CASE ("report.md: when everything passes, there is no table")
{
    auto results = test::reportResults();
    results["scenarios"] = json::array ({ results["scenarios"][7] });
    results["scenarios"][0].erase ("warnings");
    results["summary"] = sonolint::summarise (results["scenarios"]);

    const auto text = sonolint::formatMarkdown (results, {});
    CHECK (text.find ("**1 pass**\n\nNo regressions, changes, failed rules or errors.\n") != std::string::npos);
    CHECK (text.find ("| Scenario | Metric |") == std::string::npos);
}

TEST_CASE ("report.md: past the limit, the <details> block goes first and the table never")
{
    auto results = test::reportResults();
    const auto changed = results["scenarios"][0];
    results["scenarios"] = json::array();

    // 150 changed scenarios with long ids: a table of 300 rows, a chart each and 150 rows of details.
    for (int i = 0; i < 150; ++i)
    {
        auto scenario = changed;
        scenario["id"] = "a-scenario-with-a-rather-long-name-to-fill-the-report-" + std::to_string (i);
        results["scenarios"].push_back (scenario);
    }

    results["summary"] = sonolint::summarise (results["scenarios"]);
    const auto text = sonolint::formatMarkdown (results, test::reportInfo());

    CHECK (text.size() <= sonolint::kMarkdownLimit);
    CHECK (text.find ("*Truncated, see the HTML report.*") != std::string::npos);

    // Every row of the table of what didn't pass is still there.
    for (int i = 0; i < 150; ++i)
        CHECK (text.find ("| a-scenario-with-a-rather-long-name-to-fill-the-report-" + std::to_string (i)
                          + " | frequency_response.magnitude_db (channel 1) |")
               != std::string::npos);

    CHECK (text.find ("[Full report]") != std::string::npos);   // and the footer
}

TEST_CASE ("report.md: cells can't break the table")
{
    auto results = test::reportResults();
    results["scenarios"] = json::array ({ { { "id", "x" }, { "status", "error" }, { "error", "a | b\nc <d>" } } });
    results["summary"] = sonolint::summarise (results["scenarios"]);

    CHECK (sonolint::formatMarkdown (results, {}).find ("| x |  |  | a \\| b c &lt;d> |  |  | error |") != std::string::npos);
}
