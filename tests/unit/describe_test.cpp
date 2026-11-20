#include "core/report/describe.h"

#include <catch2/catch_test_macros.hpp>

using json = nlohmann::json;

namespace
{
json entry (const std::string& metric, int channel, const std::string& status, double actual, double baseline, double tolerance)
{
    return { { "metric", metric }, { "channel", channel }, { "status", status }, { "actual", actual }, { "baseline", baseline },
             { "delta", actual - baseline }, { "tolerance", { { "abs", tolerance }, { "rel", 0.0 } } } };
}

std::vector<std::string> labels (const std::vector<sonolint::Problem>& problems)
{
    std::vector<std::string> result;

    for (const auto& problem : problems)
        result.push_back (problem.metric);

    return result;
}
} // namespace

TEST_CASE ("report: channels that read the same share a row; channels that differ, or a channel that passed, don't")
{
    const json scenario { { "id", "x" }, { "status", "regressed" }, { "comparison", {
        entry ("aliasing.aliasing_dbc", 1, "regressed", -51.49, -79.8, 2.0),
        entry ("aliasing.aliasing_dbc", 2, "regressed", -51.51, -79.8, 2.0),   // reads the same: -51.5
        entry ("harmonics.thd_db", 1, "regressed", -40.0, -45.0, 1.0),
        entry ("harmonics.thd_db", 2, "regressed", -42.0, -45.0, 1.0),
        entry ("level.gain_db", 1, "changed", 0.5, 0.0, 0.05),
        entry ("level.gain_db", 2, "pass", 0.0, 0.0, 0.05),
        entry ("silence.peak_dbfs", 1, "regressed", -90.0, -100.0, 1.0),
        entry ("silence.peak_dbfs", 2, "regressed", -90.0, -100.0, 1.0),
        entry ("silence.peak_dbfs", 3, "regressed", -90.0, -100.0, 1.0) } } };

    const auto problems = sonolint::problemsOf (scenario);

    CHECK (labels (problems) == std::vector<std::string> { "aliasing.aliasing_dbc (both channels)", "silence.peak_dbfs (all 3 channels)",
                                                           "harmonics.thd_db (channel 1)", "harmonics.thd_db (channel 2)", "level.gain_db (channel 1)" });
    CHECK (problems[0].text == "-51.5 dBc (was -79.8, +28.3; tolerance ±2)");
}

TEST_CASE ("report: the worst status first and, within it, the farthest past its tolerance, across scenarios too")
{
    const json results { { "scenarios", {
        { { "id", "a" }, { "status", "changed" }, { "comparison", {
            entry ("true_peak.true_peak_dbtp", 1, "changed", 0.9, 0.0, 0.1),           // 9 tolerances
            entry ("latency.reported_samples", 1, "changed", 110.0, 122.0, 0.0) } } },  // any change is past a tolerance of 0
        { { "id", "b" }, { "status", "regressed" }, { "comparison", {
            entry ("harmonics.thdn_db", 1, "regressed", -128.0, -129.4, 1.0),          // 1.4 tolerances
            entry ("aliasing.aliasing_dbc", 1, "regressed", -51.5, -79.8, 2.0) } } } } } };   // 14 tolerances

    std::vector<std::string> order;

    for (const auto& [id, problem] : sonolint::allProblems (results))
        order.push_back (id + " " + problem.metric);

    CHECK (order == std::vector<std::string> { "b aliasing.aliasing_dbc (channel 1)", "b harmonics.thdn_db (channel 1)",
                                               "a latency.reported_samples (channel 1)", "a true_peak.true_peak_dbtp (channel 1)" });
}

TEST_CASE ("report: numbers for people, with the decimals they need")
{
    // Counts of samples stay whole.
    CHECK (sonolint::showMovement (entry ("latency.reported_samples", 1, "changed", 110.0, 122.0, 0.0)) == "110 samples (was 122, -12; tolerance ±0)");

    // As many decimals as the tolerance has.
    CHECK (sonolint::showMovement (entry ("level.gain_db", 1, "changed", 0.12, 0.0, 0.05)) == "0.12 dB (was 0.00, +0.12; tolerance ±0.05)");

    // More while the change would read as if it were within the tolerance, or the values
    // written wouldn't add up to it.
    CHECK (sonolint::showMovement (entry ("true_peak.true_peak_dbtp", 1, "changed", 0.104, 0.0, 0.1)) == "0.104 dBTP (was 0.000, +0.104; tolerance ±0.1)");
    CHECK (sonolint::showMovement (entry ("true_peak.sample_peak_dbfs", 1, "changed", -9.425, -9.875, 0.1)) == "-9.43 dBFS (was -9.88, +0.45; tolerance ±0.1)");

    // Yes or no.
    CHECK (sonolint::showMovement ({ { "metric", "determinism.bit_identical" }, { "actual", false }, { "baseline", true }, { "tolerance", { { "abs", 0.0 } } } })
           == "false (was true)");

    // A failed rule: rounding never makes the value look as if it met the rule.
    const auto rule = [] (double actual)
    {
        const json scenario { { "id", "x" }, { "status", "assertion_failed" }, { "assertions", {
            { { "metric", "true_peak.true_peak_dbtp" }, { "rule", { { "max", 0.5 } } }, { "actual", actual }, { "passed", false } } } } };
        return sonolint::problemsOf (scenario).at (0).text;
    };

    CHECK (rule (0.891238) == "0.9 dBTP, expected at most 0.5");
    CHECK (rule (0.54) == "0.54 dBTP, expected at most 0.5");
}
