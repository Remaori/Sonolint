#include "core/report/html.h"
#include "report_fixture.h"

#include <catch2/catch_test_macros.hpp>

using json = nlohmann::json;

namespace
{
size_t count (const std::string& text, const std::string& part)
{
    size_t found = 0;

    for (auto at = text.find (part); at != std::string::npos; at = text.find (part, at + part.size()))
        ++found;

    return found;
}
} // namespace

TEST_CASE ("report.html opens without internet: no scripts, no links to anything outside the page")
{
    const auto page = sonolint::formatHtml (test::reportResults(), test::reportInfo());

    CHECK (page.rfind ("<!doctype html>", 0) == 0);
    CHECK (page.find ("<script") == std::string::npos);
    CHECK (page.find ("<link") == std::string::npos);
    CHECK (page.find ("src=") == std::string::npos);
    CHECK (page.find ("@import") == std::string::npos);
    CHECK (page.find ("url(") == std::string::npos);
    CHECK (page.find ("http") == std::string::npos);
}

TEST_CASE ("report.html has a dark theme and the two charts of v0.1, with the baseline dashed")
{
    const auto page = sonolint::formatHtml (test::reportResults(), test::reportInfo());

    CHECK (page.find ("@media (prefers-color-scheme: dark)") != std::string::npos);
    CHECK (page.find ("stroke-dasharray") != std::string::npos);

    // The two channels of fat50-response are within the tolerance of each other: one chart.
    CHECK (page.find ("<h3>Frequency response, channels 1 and 2</h3>") != std::string::npos);
    CHECK (page.find ("<h3>Harmonics, channel 1</h3>") != std::string::npos);
    CHECK (count (page, "<svg class=\"chart\"") == 2);
    CHECK (count (page, "<polyline class=\"line base\"") == 1);
    CHECK (count (page, "<path class=\"bar base\"") == 2);   // H2 and H3; H4 is out of band
    CHECK (count (page, "<circle class=\"moved\"") == 1);    // only 16 kHz moved beyond 0.1 dB

    // Every status has its word next to its colour.
    CHECK (page.find ("<span class=\"status st-regressed\">regressed</span>") != std::string::npos);
    CHECK (page.find ("changed (for information)") != std::string::npos);
    CHECK (page.find ("Commit 0123456 (merged into main)") != std::string::npos);
}

TEST_CASE ("report.html: channels that differ beyond the tolerance get a chart each")
{
    auto results = test::reportResults();
    auto& scenario = results["scenarios"][0];
    scenario["metrics"]["frequency_response"]["per_channel"][1]["magnitude_db"] = json { 0.3, 0.0, -3.5 };
    scenario["comparison"][3]["actual"] = json { 0.3, 0.0, -3.5 };

    const auto curves = sonolint::curvesOf (scenario, "frequency_response", "magnitude_db");
    REQUIRE (curves.size() == 2);
    CHECK (curves[0].channels == std::vector<int> { 1 });
    CHECK (curves[1].channels == std::vector<int> { 2 });
    CHECK (sonolint::showChannels ({ 1, 2, 3 }) == "channels 1, 2 and 3");

    const auto page = sonolint::formatHtml (results, {});
    CHECK (page.find ("<h3>Frequency response, channel 1</h3>") != std::string::npos);
    CHECK (page.find ("<h3>Frequency response, channel 2</h3>") != std::string::npos);
}

TEST_CASE ("report.html escapes everything that comes from the config or the plugin")
{
    auto results = test::reportResults();
    results["plugin"]["name"] = "A <b>&</b> B";
    results["scenarios"][0]["description"] = "<script>alert(1)</script>";

    const auto page = sonolint::formatHtml (results, {});
    CHECK (page.find ("<b>") == std::string::npos);
    CHECK (page.find ("<script>") == std::string::npos);
    CHECK (page.find ("A &lt;b&gt;&amp;&lt;/b&gt; B") != std::string::npos);
    CHECK (page.find ("&lt;script&gt;alert(1)&lt;/script&gt;") != std::string::npos);
}

TEST_CASE ("report.html: the cards of what passed are folded and the others open")
{
    auto results = test::reportResults();
    const auto card = [&] (const std::string& id, bool open)
    {
        return sonolint::formatHtml (results, {}).find ("<details class=\"card\" id=\"s-" + id + "\"" + (open ? " open>" : ">")) != std::string::npos;
    };

    for (const auto* id : { "fat50-response", "transparent-1k", "thd-hot", "clip-4x", "nan", "quiet", "robustness" })
        CHECK (card (id, true));

    CHECK (card ("steady", true));   // it passed, with a warning to read
    results["scenarios"][7].erase ("warnings");
    CHECK (card ("steady", false));
}

TEST_CASE ("report.html: the detail tables share rows, and their numbers have units, a floor and no scientific notation")
{
    auto results = test::reportResults();
    results["scenarios"][1]["comparison"].push_back ({ { "metric", "level.dc" }, { "channel", 1 }, { "status", "pass" }, { "actual", 1.23e-5 }, { "baseline", 0.0 },
                                                       { "delta", 1.23e-5 }, { "tolerance", { { "abs", 1.0e-4 }, { "rel", 0.0 } } } });
    const auto page = sonolint::formatHtml (results, {});

    // Both channels of fat50-response moved alike; transparent-1k has one channel.
    CHECK (page.find ("<tr><td>frequency_response.magnitude_db</td><td>both</td><td class=\"num\">3 points</td><td class=\"num\">3 points</td>"
                      "<td class=\"num\">max Δ -0.4 dB</td><td class=\"num\">±0.1</td>") != std::string::npos);
    CHECK (page.find ("<tr><td>level.gain_db</td><td>1</td><td class=\"num\">-0.07 dB</td><td class=\"num\">-0.08 dB</td><td class=\"num\">-0.01 dB</td>") != std::string::npos);
    CHECK (page.find ("<tr><td>level.dc</td><td>1</td><td class=\"num\">0.0000</td><td class=\"num\">0.0000</td>") != std::string::npos);
    CHECK (page.find ("<td class=\"num\">below floor (-130 dB)</td><td class=\"num\">below floor (-130 dB)</td><td class=\"num\">-</td>") != std::string::npos);

    CHECK (page.find ("-300") == std::string::npos);
    CHECK (page.find ("largest") == std::string::npos);
    CHECK (page.find ("e-05") == std::string::npos);
}
