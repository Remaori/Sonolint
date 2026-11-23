#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace sonolint
{
// What every report needs to put the results into words.

// A number as people read it: up to 6 significant digits, never in scientific notation,
// and "." as the decimal point, whatever the language of the system. Anything else as JSON.
std::string showValue (const nlohmann::json& value);

// A number with so many decimals.
std::string showRounded (double value, int decimals);

// A measured value alone: "-51.5 dBc", "110 samples", "true", "-" for none, "31 points"
// for a curve, or "below floor (-130 dB)". One decimal, or as many as the default
// tolerance of the field.
std::string showMeasured (const std::string& metric, const nlohmann::json& value);

// The cells of a compared value, with the decimals of showMovement: "-79.8 dBc",
// "-51.5 dBc" and "+28.3 dB", or "max Δ 0 dB" for a curve. A change is 0 only when
// nothing changed, and "-" when both values are below the floor.
struct ComparedCells
{
    std::string baseline, actual, change;
};

ComparedCells cellsOf (const nlohmann::json& entry);

// The value an `expect` rule looked at, with the decimals of the rule, and more when
// rounding would change whether it meets the rule.
std::string showRuleValue (const nlohmann::json& assertion);

// The channels of a row they all share: "both", "all 3", or nothing for a field with one
// value for the whole plugin.
std::string showSharedChannels (const std::string& metric, size_t channels);

// An `expect` rule: "0 ± 0.05", "≤ -100", "≥ 0", "-1 to 1", "= true".
std::string showRule (const nlohmann::json& rule);

// A tolerance of the comparison: "0.05", or "0.05 + 2 % of the baseline".
std::string showTolerance (const nlohmann::json& tolerance);

// A status as the reports show it: "rule failed" for assertion_failed, the others as
// they are. results.json keeps the names of the specification.
std::string statusLabel (const std::string& status);

// How a compared value that didn't pass moved, for people: "-51.5 dBc (was -79.8, +28.3;
// tolerance ±2)", "1 of 31 points moved, the most by -0.4 dB; tolerance ±0.1",
// "false (was true)". One decimal, or the tolerance's own when it has more; the change
// is the difference of the values as written, and only when that would read as within
// the tolerance does the row get more decimals.
std::string showMovement (const nlohmann::json& entry);

// Something in a scenario that didn't pass, or that improved.
struct Problem
{
    std::string status;     // error, regressed, changed, assertion_failed, removed or improved
    std::string metric;     // "level.gain_db (channel 2)", "... (both channels)", or empty for the whole scenario
    std::string text;       // what happened, in words
    nlohmann::json source;  // the comparison entry (the first channel's, for a shared row) or the assertion; null for the whole scenario
    double excess = 0.0;    // how many tolerances the change is past; infinite for a yes/no that flipped
};

// The problems of one scenario of the results: the worst status first and, within a
// status, the farthest past its tolerance. Channels whose rows would read the same
// share one, and a value with one value for the whole plugin names no channel. A
// failed rule joins the row of its value when there is one: "0.9 dBTP (was 0.0, +0.9;
// tolerance ±0.1) · breaks rule ≤ 0.5".
std::vector<Problem> problemsOf (const nlohmann::json& scenario);

// A row of the list of what didn't pass in every scenario.
struct ListedProblem
{
    std::string scenarios;   // "fat100-hf", or the matrix variants that share the row: "robustness (bs=64, 512)"
    std::string link;        // the id of the first scenario it stands for
    Problem problem;
};

// The problems of every scenario in the order of problemsOf, across scenarios. Matrix
// variants of a scenario whose rows read the same share one.
std::vector<ListedProblem> allProblems (const nlohmann::json& results);

// A field drawn as a chart for some channels: those whose values look alike.
struct ChannelCurve
{
    std::vector<int> channels;   // the channels it stands for, from 1
    nlohmann::json actual;       // this run's values
    nlohmann::json baseline;     // null without a baseline
    std::string status;          // the worst comparison status among those channels; empty without a comparison
    nlohmann::json tolerance;    // of the comparison ({ abs, rel }), or the field's default
};

// The values of `group.field` in a scenario, with channels that coincide drawn once:
// two channels go together when every point of this run and of the baseline is within
// the tolerance of the comparison, so numerical noise doesn't split them. Empty when
// the scenario didn't measure the field.
std::vector<ChannelCurve> curvesOf (const nlohmann::json& scenario, const std::string& group, const std::string& field);

// "channel 1", "channels 1 and 2".
std::string showChannels (const std::vector<int>& channels);

// What the footer of a report says, besides the version of Sonolint.
struct ReportInfo
{
    std::string artifactUrl;   // where the full report can be downloaded; may be empty
    std::string commit;        // the commit that was measured; may be empty
    std::string mergedInto;    // the branch it was merged into before measuring; may be empty
};

// "Commit abc1234 (merged into main)": a full SHA is shortened. Empty without a commit.
std::string showCommit (const ReportInfo& info);
} // namespace sonolint
