#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace sonolint
{
// What every report needs to put the results into words.

// A number as people read it: up to 6 significant digits and "." as the decimal point,
// whatever the language of the system. Anything else as JSON.
std::string showValue (const nlohmann::json& value);

// "+0.2", "-0.4" or "0".
std::string showSigned (double value);

// A value of the baseline or of this run in a table: a number, true or false, "-" for
// none, or "31 points" for a curve.
std::string showCell (const nlohmann::json& value);

// An `expect` rule: "0 within 0.05", "at most -100", "from -1 to 1", "true".
std::string showRule (const nlohmann::json& rule);

// A tolerance of the comparison: "0.05", or "0.05 + 2 % of the baseline".
std::string showTolerance (const nlohmann::json& tolerance);

// How a compared value that didn't pass moved: "0.2, was 0 (+0.2; tolerance 0.05)",
// "1 of 31 points moved, the most by -0.4; tolerance 0.1", "false, was true".
std::string showMovement (const nlohmann::json& entry);

// Something in a scenario that didn't pass, or that improved.
struct Problem
{
    std::string status;     // error, regressed, changed, assertion_failed, removed or improved
    std::string metric;     // "level.gain_db (channel 2)", or empty when it is the whole scenario
    std::string text;       // what happened, in words
    nlohmann::json source;  // the comparison entry or the assertion it comes from; null for the whole scenario
};

// The problems of one scenario of the results, the worst first.
std::vector<Problem> problemsOf (const nlohmann::json& scenario);

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
