#include "core/compare/compare.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using json = nlohmann::json;

namespace
{
// The results of one scenario that measured `metrics`, as runScenarios gives them.
json resultsWith (const json& metrics, const std::string& id = "a", const std::string& status = "pass")
{
    return { { "scenarios", json::array ({ { { "id", id }, { "status", status }, { "metrics", metrics }, { "assertions", json::array() } } }) } };
}

// A group measured on one or two channels: what runScenarios puts in "per_channel".
json group (const json& channels) { return { { "per_channel", channels } }; }

// The one comparison entry for this field and channel.
json entryFor (const json& results, const std::string& metric, int channel = 1)
{
    for (const auto& entry : results["scenarios"][0]["comparison"])
        if (entry["metric"] == metric && entry["channel"] == channel)
            return entry;

    return {};
}

// Compares a single value of one field against a baseline value and returns the entry.
json compareOne (const std::string& group, const std::string& field, const json& actual, const json& stored,
                 const std::map<std::string, sonolint::Tolerance>& tolerances = {})
{
    auto results = resultsWith ({ { group, ::group (json::array ({ { { field, actual } } })) } });
    const json section { { "a", { { group, json::array ({ { { field, stored } } }) } } } };
    sonolint::compareWithBaseline (results, section, {}, tolerances, {});
    return entryFor (results, group + "." + field);
}

std::string statusOf (const std::string& group, const std::string& field, const json& actual, const json& stored,
                      const std::map<std::string, sonolint::Tolerance>& tolerances = {})
{
    return compareOne (group, field, actual, stored, tolerances)["status"].get<std::string>();
}
} // namespace

TEST_CASE ("compare: each field gets the status the specification gives it")
{
    struct Case
    {
        std::string group, field;
        json actual, stored;
        std::string expected;
    };

    const std::vector<Case> cases {
        // target: within the tolerance passes, outside it in either direction is a change.
        { "level", "gain_db", -6.04, -6.0, "pass" },
        { "level", "gain_db", -6.06, -6.0, "changed" },
        { "level", "gain_db", -5.94, -6.0, "changed" },
        { "level", "dc", 0.00005, 0.0, "pass" },
        { "level", "dc", 0.0002, 0.0, "changed" },

        // lower: going down beyond the tolerance is better, going up is worse.
        { "harmonics", "thd_db", -61.0, -60.0, "pass" },
        { "harmonics", "thd_db", -62.0, -60.0, "improved" },
        { "harmonics", "thd_db", -58.5, -60.0, "regressed" },

        // Below -130 dB everything is the same: noise in a transparent plugin isn't a change.
        { "harmonics", "thdn_db", -150.0, -131.0, "pass" },
        { "harmonics", "thdn_db", -125.0, -150.0, "regressed" },

        // Values without a tolerance must be equal once rounded.
        { "aliasing", "components", 23, 23, "pass" },
        { "aliasing", "components", 22, 23, "changed" },
        { "latency", "reported_samples", 122, 122, "pass" },
        { "latency", "reported_samples", 121, 122, "changed" },
        { "harmonics", "f0_hz", 999.756, 999.76, "pass" },

        // Yes/no values and points without a value: exactly equal.
        { "determinism", "bit_identical", true, true, "pass" },
        { "determinism", "bit_identical", false, true, "changed" },

        // Curves: point by point, and a different length is a change.
        { "frequency_response", "magnitude_db", json { 0.0, -3.0, -6.05 }, json { 0.0, -3.0, -6.0 }, "pass" },
        { "frequency_response", "magnitude_db", json { 0.0, -3.2, -6.0 }, json { 0.0, -3.0, -6.0 }, "changed" },
        { "frequency_response", "magnitude_db", json { 0.0, -3.0 }, json { 0.0, -3.0, -6.0 }, "changed" },
        { "harmonics", "h_dbc", json { -40.0, nullptr }, json { -40.5, nullptr }, "pass" },
        { "harmonics", "h_dbc", json { -38.0, -60.0 }, json { -40.0, -61.0 }, "regressed" },
        { "harmonics", "h_dbc", json { -45.0, -60.0 }, json { -40.0, -61.0 }, "improved" },
        { "harmonics", "h_dbc", json { -40.0, -60.0 }, json { -40.0, nullptr }, "changed" },
    };

    for (const auto& test : cases)
    {
        CAPTURE (test.group, test.field, test.actual, test.stored);
        CHECK (statusOf (test.group, test.field, test.actual, test.stored) == test.expected);
    }
}

TEST_CASE ("compare: the entry says how far the value moved and with which tolerance")
{
    const auto entry = compareOne ("level", "gain_db", -5.8, -6.0);

    CHECK (entry["status"] == "changed");
    CHECK (entry["actual"] == -5.8);
    CHECK (entry["baseline"] == -6.0);
    CHECK_THAT (entry["delta"].get<double>(), WithinAbs (0.2, 1.0e-9));
    CHECK (entry["tolerance"] == json { { "abs", 0.05 }, { "rel", 0.0 } });
    CHECK_FALSE (entry.contains ("report_only"));

    // A curve reports the point that moved the most and how many are outside.
    const auto curve = compareOne ("frequency_response", "magnitude_db", json { 0.0, -3.4, -6.2 }, json { 0.0, -3.0, -6.0 });
    CHECK_THAT (curve["delta"].get<double>(), WithinAbs (-0.4, 1.0e-9));
    CHECK (curve["points_outside"] == 2);
}

TEST_CASE ("compare: the actual value is rounded like the baseline before comparing")
{
    // A run that matches the baseline it just wrote compares equal, whatever the extra decimals.
    CHECK (compareOne ("level", "dc", 1.2345678e-5, 1.23457e-5)["status"] == "pass");
    CHECK (compareOne ("level", "dc", 1.2345678e-5, 1.23457e-5)["actual"] == 1.23457e-5);
    CHECK (compareOne ("determinism", "max_diff_dbfs", -100.12349, -100.123)["status"] == "pass");
}

TEST_CASE ("compare: abs and rel add up, and the config replaces the defaults")
{
    using T = sonolint::Tolerance;

    // 2 % of -10 dB is 0.2 dB, plus 0.05 dB: 0.25 dB in all.
    CHECK (statusOf ("level", "gain_db", -10.24, -10.0, { { "level.gain_db", T { 0.05, 0.02, false } } }) == "pass");
    CHECK (statusOf ("level", "gain_db", -10.26, -10.0, { { "level.gain_db", T { 0.05, 0.02, false } } }) == "changed");

    // An entry that sets only rel keeps the default abs.
    CHECK (statusOf ("level", "gain_db", -10.24, -10.0, { { "level.gain_db", T { {}, 0.02, false } } }) == "pass");

    // A glob, and an exact path that beats it.
    CHECK (statusOf ("level", "gain_db", -6.4, -6.0, { { "level.*", T { 0.5, {}, false } } }) == "pass");
    CHECK (statusOf ("level", "gain_db", -6.4, -6.0, { { "level.*", T { 0.5, {}, false } }, { "level.gain_db", T { 0.1, {}, false } } }) == "changed");

    // Among globs, the longest one.
    CHECK (statusOf ("level", "gain_db", -6.4, -6.0, { { "*", T { 0.01, {}, false } }, { "level.*", T { 0.5, {}, false } } }) == "pass");
}

TEST_CASE ("compare: the scenario's own tolerances win over the ones for every scenario")
{
    sonolint::Scenario scenario;
    scenario.id = "a";
    scenario.tolerances["level.gain_db"] = { 1.0, {}, false };

    auto results = resultsWith ({ { "level", group (json::array ({ { { "gain_db", -5.5 } } })) } });
    const json section { { "a", { { "level", json::array ({ { { "gain_db", -6.0 } } }) } } } };
    sonolint::compareWithBaseline (results, section, { scenario }, { { "level.gain_db", { 0.1, {}, false } } }, {});

    CHECK (entryFor (results, "level.gain_db")["status"] == "pass");
}

TEST_CASE ("compare: phase is compared wrapped to plus or minus 180 degrees, and never fails")
{
    // 179 and -179 degrees are 2 degrees apart: within the default tolerance.
    const auto wrapped = compareOne ("frequency_response", "phase_deg", json { 179.0 }, json { -179.0 });
    CHECK (wrapped["status"] == "pass");
    CHECK_THAT (wrapped["delta"].get<double>(), WithinAbs (-2.0, 1.0e-9));

    // 10 degrees is a change, shown but only for information.
    auto results = resultsWith ({ { "frequency_response", group (json::array ({ { { "phase_deg", json { 10.0 } } } })) } });
    const json section { { "a", { { "frequency_response", json::array ({ { { "phase_deg", json { 0.0 } } } }) } } } };
    sonolint::compareWithBaseline (results, section, {}, {}, {});

    CHECK (entryFor (results, "frequency_response.phase_deg")["status"] == "changed");
    CHECK (entryFor (results, "frequency_response.phase_deg")["report_only"] == true);
    CHECK (results["scenarios"][0]["status"] == "pass");
    CHECK_FALSE (sonolint::fails (results, sonolint::FailOn::regressions));

    // report_only in the config does the same to any field.
    auto quiet = resultsWith ({ { "level", group (json::array ({ { { "gain_db", -3.0 } } })) } });
    const json gainSection { { "a", { { "level", json::array ({ { { "gain_db", -6.0 } } }) } } } };
    sonolint::compareWithBaseline (quiet, gainSection, {}, { { "level.*", { {}, {}, true } } }, {});
    CHECK (quiet["scenarios"][0]["status"] == "pass");
}

TEST_CASE ("compare: channel by channel, so a change in one channel alone is caught")
{
    // The right channel lost 1 dB; the left one, the loudest, didn't move.
    auto results = resultsWith ({ { "level", group (json::array ({ { { "gain_db", 0.0 } }, { { "gain_db", -1.0 } } })) } });
    const json section { { "a", { { "level", json::array ({ { { "gain_db", 0.0 } }, { { "gain_db", 0.0 } } }) } } } };
    sonolint::compareWithBaseline (results, section, {}, {}, {});

    CHECK (entryFor (results, "level.gain_db", 1)["status"] == "pass");
    CHECK (entryFor (results, "level.gain_db", 2)["status"] == "changed");
    CHECK (results["scenarios"][0]["status"] == "changed");

    // From stereo to mono: the second channel's values are removed.
    auto mono = resultsWith ({ { "level", group (json::array ({ { { "gain_db", 0.0 } } })) } });
    sonolint::compareWithBaseline (mono, section, {}, {}, {});
    CHECK (entryFor (mono, "level.gain_db", 2)["status"] == "removed");
}

TEST_CASE ("compare: new and removed fields and scenarios")
{
    // No baseline at all: every value is new, and nothing fails.
    auto fresh = resultsWith ({ { "level", group (json::array ({ { { "gain_db", 0.0 } } })) } });
    sonolint::compareWithBaseline (fresh, json(), {}, {}, {});
    CHECK (entryFor (fresh, "level.gain_db")["status"] == "new");
    CHECK (fresh["scenarios"][0]["status"] == "new");
    CHECK_FALSE (sonolint::fails (fresh, sonolint::FailOn::regressions));

    // A metric group no longer measured, and one measured for the first time.
    auto results = resultsWith ({ { "silence", group (json::array ({ { { "noise_floor_dbfs", -300.0 }, { "peak_dbfs", -300.0 } } })) } });
    const json section { { "a", { { "level", json::array ({ { { "gain_db", 0.0 } } }) } } },
                         { "renamed", { { "level", json::array ({ { { "gain_db", 0.0 } } }) } } },
                         { "other", { { "level", json::array ({ { { "gain_db", 0.0 } } }) } } } };
    sonolint::compareWithBaseline (results, section, {}, {}, {});

    CHECK (entryFor (results, "level.gain_db")["status"] == "removed");
    CHECK (entryFor (results, "silence.noise_floor_dbfs")["status"] == "new");
    CHECK (results["scenarios"][0]["status"] == "removed");

    // The baseline's scenarios that didn't run come back as removed...
    REQUIRE (results["scenarios"].size() == 3);
    CHECK (results["scenarios"][1] == json { { "id", "other" }, { "status", "removed" } });
    CHECK (results["scenarios"][2] == json { { "id", "renamed" }, { "status", "removed" } });
    CHECK (results["summary"]["removed"] == 3);

    // ...except, with --filter, those outside it.
    auto filtered = resultsWith ({ { "level", group (json::array ({ { { "gain_db", 0.0 } } })) } });
    sonolint::compareWithBaseline (filtered, section, {}, {}, { "a", "ren*" });
    REQUIRE (filtered["scenarios"].size() == 2);
    CHECK (filtered["scenarios"][1]["id"] == "renamed");
}

TEST_CASE ("compare: a scenario takes the worst status of its values and its rules, and errors are left alone")
{
    // A failed rule and a changed value: the change is worse.
    auto results = resultsWith ({ { "level", group (json::array ({ { { "gain_db", -1.0 } } })) } }, "a", "assertion_failed");
    results["scenarios"][0]["assertions"] = json::array ({ { { "metric", "level.gain_db" }, { "passed", false } } });
    const json section { { "a", { { "level", json::array ({ { { "gain_db", 0.0 } } }) } } } };
    sonolint::compareWithBaseline (results, section, {}, {}, {});
    CHECK (results["scenarios"][0]["status"] == "changed");

    // ...and an improved value doesn't hide the failed rule.
    auto improved = resultsWith ({ { "harmonics", group (json::array ({ { { "thd_db", -70.0 } } })) } }, "a", "assertion_failed");
    sonolint::compareWithBaseline (improved, json { { "a", { { "harmonics", json::array ({ { { "thd_db", -60.0 } } }) } } } }, {}, {}, {});
    CHECK (improved["scenarios"][0]["status"] == "assertion_failed");

    // A scenario that ended in error has nothing to compare.
    auto broken = json { { "scenarios", json::array ({ { { "id", "a" }, { "status", "error" }, { "error", "NaN" } } }) } };
    sonolint::compareWithBaseline (broken, section, {}, {}, {});
    CHECK (broken["scenarios"][0]["status"] == "error");
    CHECK_FALSE (broken["scenarios"][0].contains ("comparison"));
    CHECK (broken["summary"]["error"] == 1);
}

TEST_CASE ("compare: --fail-on decides what fails")
{
    const auto withStatus = [] (const std::string& status, bool ruleFailed = false)
    {
        json scenario { { "id", "a" }, { "status", status }, { "assertions", json::array() } };

        if (ruleFailed)
            scenario["assertions"].push_back ({ { "metric", "level.gain_db" }, { "passed", false } });

        return json { { "scenarios", json::array ({ scenario }) } };
    };

    using sonolint::FailOn;

    for (const auto* status : { "regressed", "changed", "assertion_failed", "error" })
    {
        CAPTURE (status);
        CHECK (sonolint::fails (withStatus (status, std::string (status) == "assertion_failed"), FailOn::regressions));
    }

    for (const auto* status : { "pass", "new", "improved", "removed" })
    {
        CAPTURE (status);
        CHECK_FALSE (sonolint::fails (withStatus (status), FailOn::regressions));
    }

    CHECK_FALSE (sonolint::fails (withStatus ("changed"), FailOn::assertions));
    CHECK (sonolint::fails (withStatus ("changed", true), FailOn::assertions));   // the rule hides behind the change
    CHECK (sonolint::fails (withStatus ("error"), FailOn::assertions));
    CHECK_FALSE (sonolint::fails (withStatus ("error", true), FailOn::none));
}
