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

json failedRule (const std::string& metric, double max, double actual)
{
    return { { "metric", metric }, { "rule", { { "max", max } } }, { "actual", actual }, { "passed", false } };
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
        entry ("silence.peak_dbfs", 3, "regressed", -90.0, -100.0, 1.0),
        entry ("latency.reported_samples", 1, "changed", 110.0, 122.0, 0.0),   // one value for the whole plugin
        entry ("latency.reported_samples", 2, "changed", 110.0, 122.0, 0.0) } } };

    const auto problems = sonolint::problemsOf (scenario);

    CHECK (labels (problems) == std::vector<std::string> { "aliasing.aliasing_dbc (both channels)", "silence.peak_dbfs (all 3 channels)",
                                                           "harmonics.thd_db (channel 1)", "harmonics.thd_db (channel 2)", "latency.reported_samples",
                                                           "level.gain_db (channel 1)" });
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

    for (const auto& row : sonolint::allProblems (results))
        order.push_back (row.scenarios + " " + row.problem.metric);

    CHECK (order == std::vector<std::string> { "b aliasing.aliasing_dbc (channel 1)", "b harmonics.thdn_db (channel 1)",
                                               "a latency.reported_samples (channel 1)", "a true_peak.true_peak_dbtp (channel 1)" });
}

TEST_CASE ("report: matrix variants whose rows read the same share one")
{
    const auto variant = [] (const std::string& id, double reported)
    {
        return json { { "id", id }, { "status", "changed" }, { "comparison", {
            entry ("latency.reported_samples", 1, "changed", reported, 122.0, 0.0),
            entry ("latency.reported_samples", 2, "changed", reported, 122.0, 0.0) } } };
    };

    const json results { { "scenarios", { variant ("robustness@bs=64", 110.0), variant ("robustness@bs=512", 110.0), variant ("robustness@bs=1024", 100.0) } } };
    const auto rows = sonolint::allProblems (results);

    REQUIRE (rows.size() == 2);
    CHECK (rows[0].scenarios == "robustness (bs=64, 512)");
    CHECK (rows[0].link == "robustness@bs=64");
    CHECK (rows[0].problem.metric == "latency.reported_samples");
    CHECK (rows[1].scenarios == "robustness@bs=1024");
}

TEST_CASE ("report: a failed rule joins the row of its value, and stands alone without one")
{
    const auto scenario = [] (double actual, double baseline)
    {
        return json { { "id", "x" }, { "status", "changed" },
                      { "comparison", { entry ("true_peak.true_peak_dbtp", 1, "changed", actual, baseline, 0.1),
                                        entry ("true_peak.true_peak_dbtp", 2, "changed", actual, baseline, 0.1) } },
                      { "assertions", { failedRule ("true_peak.true_peak_dbtp", 0.5, actual), failedRule ("level.gain_db", 0.0, 0.25) } } };
    };

    const auto problems = sonolint::problemsOf (scenario (0.891, 0.0));

    REQUIRE (problems.size() == 2);
    CHECK (problems[0].metric == "true_peak.true_peak_dbtp (both channels)");
    CHECK (problems[0].text == "0.9 dBTP (was 0.0, +0.9; tolerance ±0.1) · breaks rule ≤ 0.5");
    CHECK (problems[0].status == "changed");
    CHECK (problems[1].metric == "level.gain_db");
    CHECK (problems[1].text == "0.3 dB · breaks rule ≤ 0");   // one decimal is enough: 0.3 still breaks it
    CHECK (sonolint::statusLabel (problems[1].status) == "rule failed");

    // With one decimal, 0.54 would read as 0.5, which meets the rule.
    CHECK (sonolint::problemsOf (scenario (0.54, 0.3))[0].text == "0.54 dBTP (was 0.30, +0.24; tolerance ±0.1) · breaks rule ≤ 0.5");
}

TEST_CASE ("report: numbers for people, with the decimals they need")
{
    // Counts of samples stay whole.
    CHECK (sonolint::showMovement (entry ("latency.reported_samples", 1, "changed", 110.0, 122.0, 0.0)) == "110 samples (was 122, -12; tolerance ±0)");

    // As many decimals as the tolerance has.
    CHECK (sonolint::showMovement (entry ("level.gain_db", 1, "changed", 0.12, 0.0, 0.05)) == "0.12 dB (was 0.00, +0.12; tolerance ±0.05)");

    // The change is the difference of the values as written, so the row adds up.
    CHECK (sonolint::showMovement (entry ("true_peak.sample_peak_dbfs", 1, "changed", -9.425, -9.875, 0.1)) == "-9.4 dBFS (was -9.9, +0.5; tolerance ±0.1)");

    // More decimals only when fewer would read as within the tolerance.
    CHECK (sonolint::showMovement (entry ("true_peak.true_peak_dbtp", 1, "changed", 0.104, 0.0, 0.1)) == "0.104 dBTP (was 0.000, +0.104; tolerance ±0.1)");

    // Yes or no.
    CHECK (sonolint::showMovement ({ { "metric", "determinism.bit_identical" }, { "actual", false }, { "baseline", true }, { "tolerance", { { "abs", 0.0 } } } })
           == "false (was true)");

    // A failed rule alone: rounding never makes the value look as if it met the rule.
    const auto rule = [] (double actual)
    {
        const json scenario { { "id", "x" }, { "status", "assertion_failed" }, { "assertions", { failedRule ("true_peak.true_peak_dbtp", 0.5, actual) } } };
        return sonolint::problemsOf (scenario).at (0).text;
    };

    CHECK (rule (0.891238) == "0.9 dBTP · breaks rule ≤ 0.5");
    CHECK (rule (0.54) == "0.54 dBTP · breaks rule ≤ 0.5");
}

TEST_CASE ("report: below the floor of the comparison there is no number, and the change counts from the floor")
{
    // The comparison clamps dB values at -130, so this entry's delta is 7.7 and not 27.7.
    auto moved = entry ("harmonics.thdn_db", 1, "regressed", -122.3, -150.0, 1.0);
    moved["delta"] = 7.7;

    CHECK (sonolint::showMovement (moved) == "-122.3 dB (was below floor (-130 dB), +7.7; tolerance ±1)");

    const auto cells = sonolint::cellsOf (moved);
    CHECK (cells.baseline == "below floor (-130 dB)");
    CHECK (cells.actual == "-122.3 dB");
    CHECK (cells.change == "+7.7 dB");

    // Both below it: different numbers, and nothing to compare.
    auto equal = entry ("harmonics.thdn_db", 1, "pass", -138.2, -150.0, 1.0);
    equal["delta"] = 0.0;
    CHECK (sonolint::cellsOf (equal).actual == "below floor (-130 dB)");
    CHECK (sonolint::cellsOf (equal).change == "-");

    // Only dB values have a floor, and a failed rule shows its number wherever it is.
    CHECK (sonolint::showMeasured ("silence.noise_floor_dbfs", -300.0) == "below floor (-130 dB)");
    CHECK (sonolint::showMeasured ("latency.mismatch_samples", -200.0) == "-200.0 samples");

    auto rule = failedRule ("silence.noise_floor_dbfs", -140.0, -135.0);
    CHECK (sonolint::showRuleValue (rule) == "-135.0 dBFS");
    rule["rule"]["max"] = -120.0;
    rule["passed"] = true;
    CHECK (sonolint::showRuleValue (rule) == "below floor (-130 dB)");
}

TEST_CASE ("report: the cells of the detail tables are written like the rows of what didn't pass")
{
    const auto cells = sonolint::cellsOf (entry ("aliasing.aliasing_dbc", 1, "regressed", -51.493, -79.796, 2.0));
    CHECK (cells.baseline == "-79.8 dBc");
    CHECK (cells.actual == "-51.5 dBc");
    CHECK (cells.change == "+28.3 dB");   // between two values in dBc, plain dB

    // 0 only when nothing changed; a change smaller than a decimal is 0.0.
    CHECK (sonolint::cellsOf (entry ("harmonics.thd_db", 1, "pass", -17.809, -17.809, 1.0)).change == "0 dB");
    CHECK (sonolint::cellsOf (entry ("harmonics.thd_db", 1, "pass", -17.807, -17.809, 1.0)).change == "0.0 dB");

    // A value that passes needs no extra decimals, however close to the tolerance.
    CHECK (sonolint::cellsOf (entry ("true_peak.true_peak_dbtp", 1, "pass", 0.1, 0.0, 0.1)).actual == "0.1 dBTP");

    const json curve { { "metric", "harmonics.h_dbc" }, { "channel", 1 }, { "status", "pass" }, { "actual", { -17.8, nullptr } }, { "baseline", { -17.8, nullptr } },
                       { "delta", 0.0 }, { "points_outside", 0 }, { "tolerance", { { "abs", 1.5 }, { "rel", 0.0 } } } };
    CHECK (sonolint::cellsOf (curve).actual == "2 points");
    CHECK (sonolint::cellsOf (curve).change == "max Δ 0 dB");

    // A value without a baseline.
    const json added { { "metric", "latency.reported_samples" }, { "channel", 1 }, { "status", "new" }, { "actual", 122 }, { "tolerance", { { "abs", 0.0 } } } };
    CHECK (sonolint::cellsOf (added).baseline == "-");
    CHECK (sonolint::cellsOf (added).actual == "122 samples");
    CHECK (sonolint::cellsOf (added).change.empty());
}

TEST_CASE ("report: no number is written in scientific notation")
{
    CHECK (sonolint::showValue (4.12e-5) == "0.000041");
    CHECK (sonolint::showValue (-4.0e-8) == "0");
    CHECK (sonolint::showValue (1234567.0) == "1234567");
    CHECK (sonolint::showMeasured ("level.dc", 1.23e-5) == "0.0000");
}

TEST_CASE ("report: a rule broken by some channels names them, and joins only a row that stands for the same ones")
{
    auto rule = failedRule ("level.gain_db", 0.1, 0.5);
    rule["channels"] = { 2 };
    CHECK (sonolint::showRuleMetric (rule) == "level.gain_db (channel 2)");

    const auto rowsWith = [&] (const std::string& firstChannel, double first)
    {
        const json scenario { { "id", "x" }, { "status", "changed" }, { "assertions", { rule } },
                              { "comparison", { entry ("level.gain_db", 1, firstChannel, first, 0.0, 0.05), entry ("level.gain_db", 2, "changed", 0.5, 0.0, 0.05) } } };
        return sonolint::problemsOf (scenario);
    };

    // Only channel 2 moved: the same channel that breaks the rule.
    auto rows = rowsWith ("pass", 0.0);
    REQUIRE (rows.size() == 1);
    CHECK (rows[0].metric == "level.gain_db (channel 2)");
    CHECK (rows[0].text == "0.50 dB (was 0.00, +0.50; tolerance ±0.05) · breaks rule ≤ 0.1");

    // Both moved alike, and one of them breaks it: the rule says which on its own row.
    rows = rowsWith ("changed", 0.5);
    REQUIRE (rows.size() == 2);
    CHECK (rows[0].metric == "level.gain_db (both channels)");
    CHECK (rows[0].text.find ("breaks") == std::string::npos);
    CHECK (rows[1].metric == "level.gain_db (channel 2)");
    CHECK (rows[1].text == "0.5 dB · breaks rule ≤ 0.1");
}
