#include "core/report/describe.h"

#include "core/compare/catalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

std::string metricLabel (const json& entry)
{
    return entry.at ("metric").get<std::string>() + (entry.contains ("channel") ? " (channel " + std::to_string (entry["channel"].get<int>()) + ")" : "");
}


int severity (const std::string& status)
{
    static const std::vector<std::string> order { "error", "regressed", "changed", "assertion_failed", "removed", "improved" };
    const auto found = std::find (order.begin(), order.end(), status);
    return (int) (found - order.begin());
}

bool worse (const Problem& a, const Problem& b)
{
    return severity (a.status) != severity (b.status) ? severity (a.status) < severity (b.status) : a.excess > b.excess;
}

// " dBc" for aliasing.aliasing_dbc: the unit is the end of the field's name.
std::string unitOf (const std::string& metric)
{
    static const std::vector<std::pair<std::string, std::string>> units {
        { "_dbc", "dBc" }, { "_dbtp", "dBTP" }, { "_dbfs", "dBFS" }, { "_db", "dB" }, { "_samples", "samples" }, { "_hz", "Hz" }, { "_deg", "deg" } };

    for (const auto& [suffix, unit] : units)
        if (metric.size() > suffix.size() && metric.compare (metric.size() - suffix.size(), suffix.size(), suffix) == 0)
            return " " + unit;

    return {};
}

double roundedTo (double value, int decimals) { return std::round (value * std::pow (10.0, decimals)) / std::pow (10.0, decimals); }

// The fewest decimals, up to 6, that write the number exactly: 2 for 0.05.
int decimalsOf (double value)
{
    int decimals = 0;

    while (decimals < 6 && std::abs (roundedTo (value, decimals) - value) > 1.0e-9)
        ++decimals;

    return decimals;
}

bool integral (const json& value) { return value.is_number() && value.get<double>() == std::round (value.get<double>()); }

std::string fixed (double value, int decimals)
{
    std::ostringstream out;
    out.imbue (std::locale::classic());
    out << std::fixed << std::setprecision (decimals) << roundedTo (value, decimals) + 0.0;   // + 0.0 turns -0 into 0
    return out.str();
}

std::string fixedSigned (double value, int decimals)
{
    const auto text = fixed (value, decimals);
    return text.find_first_not_of ("-0.") != std::string::npos && value > 0.0 ? "+" + text : text;
}

// The tolerance a value was compared with: abs plus rel of the baseline.
double toleranceOf (const json& entry)
{
    const auto& tolerance = entry.at ("tolerance");
    const auto& baseline = entry.value ("baseline", json());
    return tolerance.value ("abs", 0.0) + tolerance.value ("rel", 0.0) * (baseline.is_number() ? std::abs (baseline.get<double>()) : 0.0);
}

// Whether a value would meet an expect rule.
bool meets (double value, const json& rule)
{
    if (rule.contains ("approx"))   return std::abs (value - rule["approx"].get<double>()) <= rule["tol"].get<double>();
    if (rule.contains ("min"))      return value >= rule["min"].get<double>();
    if (rule.contains ("max"))      return value <= rule["max"].get<double>();
    if (rule.contains ("between"))  return value >= rule["between"][0].get<double>() && value <= rule["between"][1].get<double>();

    return rule.at ("equals").is_number() && value == rule["equals"].get<double>();
}

// "0.9 dBTP · breaks rule ≤ 0.5": as many decimals as the rule has, at least one, and
// more while the rounded value would meet the rule.
std::string showBrokenRule (const json& assertion)
{
    const auto& actual = assertion.at ("actual");
    const auto& rule = assertion.at ("rule");

    if (actual.is_array())
        return "some points break rule " + showRule (rule);

    if (! actual.is_number())
        return showValue (actual) + " · breaks rule " + showRule (rule);

    auto decimals = 0;
    auto exact = integral (actual);

    for (const auto& [key, limit] : rule.items())
        for (const auto& number : limit.is_array() ? limit : json::array ({ limit }))
            if (number.is_number())
            {
                decimals = std::max (decimals, decimalsOf (number.get<double>()));
                exact = exact && integral (number);
            }

    decimals = exact ? 0 : std::max (1, decimals);

    while (decimals < 6 && meets (roundedTo (actual.get<double>(), decimals), rule))
        ++decimals;

    return fixed (actual.get<double>(), decimals) + unitOf (assertion.at ("metric").get<std::string>()) + " · breaks rule " + showRule (rule);
}

// How a compared value moved, and the rule it breaks, if `rule` isn't null. One decimal,
// or the tolerance's own when it has more, for every number of the row; the change is
// the difference of the values as written, so the row adds up. More decimals only when
// fewer would read as within the tolerance, or as meeting the rule.
std::string movementText (const json& entry, const json& rule)
{
    const auto& actual = entry.at ("actual");
    const auto& baseline = entry.at ("baseline");
    const auto breaks = rule.is_null() ? std::string() : " · breaks rule " + showRule (rule);

    if (actual.is_array() && baseline.is_array() && actual.size() != baseline.size())
        return std::to_string (actual.size()) + " points, and the baseline has " + std::to_string (baseline.size()) + breaks;

    if (! entry.contains ("delta"))
        return showValue (actual) + " (was " + showValue (baseline) + ")" + breaks;

    const auto delta = entry["delta"].get<double>();
    const auto tolerance = toleranceOf (entry);
    const auto unit = unitOf (entry.at ("metric").get<std::string>());
    const auto scalar = ! actual.is_array();
    const auto exact = integral (entry["delta"]) && integral (json (tolerance)) && (! scalar || (integral (actual) && integral (baseline)));
    auto decimals = exact ? 0 : std::max (1, decimalsOf (entry.at ("tolerance").value ("abs", 0.0)));

    const auto shown = [&] (int places) { return scalar ? roundedTo (actual.get<double>(), places) - roundedTo (baseline.get<double>(), places) : delta; };

    while (decimals < 6 && (roundedTo (std::abs (shown (decimals)), decimals) <= tolerance
                            || (scalar && ! rule.is_null() && meets (roundedTo (actual.get<double>(), decimals), rule))))
        ++decimals;

    const auto within = "tolerance ±" + showTolerance (entry.at ("tolerance"));

    if (! scalar)
        return std::to_string (entry.at ("points_outside").get<int>()) + " of " + std::to_string (actual.size()) + " points moved, the most by "
             + fixedSigned (delta, decimals) + unit + "; " + within + breaks;

    return fixed (actual.get<double>(), decimals) + unit + " (was " + fixed (baseline.get<double>(), decimals) + ", "
         + fixedSigned (shown (decimals), decimals) + "; " + within + ")" + breaks;
}

// Whether a field has one value for the whole plugin, like the latency it reports.
bool perPlugin (const std::string& metric)
{
    const auto dot = metric.find ('.');
    const auto group = metricCatalog().find (metric.substr (0, dot));

    if (dot == std::string::npos || group == metricCatalog().end())
        return false;

    const auto& fields = group->second;
    const auto info = std::find_if (fields.begin(), fields.end(), [&] (const FieldInfo& f) { return f.name == metric.substr (dot + 1); });
    return info != fields.end() && info->perPlugin;
}

// "bs=64, 512" when the variants differ in one setting; else "sr=44100,bs=64; sr=48000,bs=64".
std::string showVariants (const std::vector<std::string>& variants)
{
    const auto key = variants.front().substr (0, variants.front().find ('=') + 1);
    const auto oneKey = std::all_of (variants.begin(), variants.end(), [&] (const std::string& variant)
    {
        return variant.find (',') == std::string::npos && variant.rfind (key, 0) == 0;
    });

    std::string text;

    for (const auto& variant : variants)
        text += text.empty() ? variant : oneKey ? ", " + variant.substr (key.size()) : "; " + variant;

    return text;
}
} // namespace

std::string showValue (const json& value)
{
    if (! value.is_number_float())
        return value.dump();

    std::ostringstream out;
    out.imbue (std::locale::classic());
    out << std::setprecision (6) << value.get<double>();
    return out.str();
}

std::string showMovement (const json& entry) { return movementText (entry, json()); }

std::string showSigned (double value) { return (value > 0.0 ? "+" : "") + showValue (value); }

std::string showCell (const json& value)
{
    if (value.is_null())
        return "-";

    if (value.is_array())
        return std::to_string (value.size()) + " points";

    return showValue (value);
}

std::string showRule (const json& rule)
{
    if (rule.contains ("approx"))   return showValue (rule["approx"]) + " ± " + showValue (rule["tol"]);
    if (rule.contains ("min"))      return "≥ " + showValue (rule["min"]);
    if (rule.contains ("max"))      return "≤ " + showValue (rule["max"]);
    if (rule.contains ("between"))  return showValue (rule["between"][0]) + " to " + showValue (rule["between"][1]);

    return "= " + showValue (rule.at ("equals"));
}

std::string statusLabel (const std::string& status) { return status == "assertion_failed" ? "rule failed" : status; }

std::string showTolerance (const json& tolerance)
{
    const auto rel = tolerance.value ("rel", 0.0);
    return showValue (tolerance.value ("abs", 0.0)) + (rel > 0.0 ? " + " + showValue (100.0 * rel) + " % of the baseline" : "");
}

std::vector<Problem> problemsOf (const json& scenario)
{
    std::vector<Problem> problems;
    const auto status = scenario.at ("status").get<std::string>();

    if (status == "error")
        problems.push_back ({ "error", "", scenario.at ("error").get<std::string>(), {} });

    if (status == "removed")
        problems.push_back ({ "removed", "", "in the baseline, but no longer in the config", {} });

    const auto assertions = scenario.value ("assertions", json::array());
    std::vector<bool> joined (assertions.size(), false);   // failed rules shown in the row of their value

    const auto comparison = scenario.value ("comparison", json::array());
    std::vector<std::string> metrics;

    for (const auto& entry : comparison)
        if (const auto metric = entry.at ("metric").get<std::string>(); std::find (metrics.begin(), metrics.end(), metric) == metrics.end())
            metrics.push_back (metric);

    for (const auto& metric : metrics)
    {
        std::vector<Problem> rows;
        size_t channels = 0;

        for (const auto& entry : comparison)
        {
            if (entry.at ("metric") != metric)
                continue;

            ++channels;
            const auto moved = entry.at ("status").get<std::string>();

            if (entry.value ("report_only", false))
                continue;   // shown in full in the HTML report; it never changes the status

            if (moved == "regressed" || moved == "changed" || moved == "improved")
            {
                const auto delta = std::abs (entry.value ("delta", HUGE_VAL));
                const auto tolerance = toleranceOf (entry);
                rows.push_back ({ moved, metricLabel (entry), showMovement (entry), entry, tolerance > 0.0 ? delta / tolerance : delta > 0.0 ? HUGE_VAL : 0.0 });
            }
            else if (moved == "removed")
            {
                rows.push_back ({ moved, metricLabel (entry), "in the baseline, but no longer measured", entry });
            }
        }

        // Every channel moved and reads the same: one row for all of them.
        const auto shared = channels > 1 && rows.size() == channels && std::all_of (rows.begin(), rows.end(), [&] (const Problem& row)
        {
            return row.status == rows.front().status && row.text == rows.front().text;
        });

        if (shared)
        {
            rows.resize (1);
            rows.front().metric = metric + (perPlugin (metric) ? "" : channels == 2 ? " (both channels)" : " (all " + std::to_string (channels) + " channels)");
        }

        // A failed rule of this value joins its row, when the value has just one.
        for (size_t a = 0; a < assertions.size() && rows.size() == 1 && rows.front().status != "removed"; ++a)
        {
            if (assertions[a].at ("passed").get<bool>() || assertions[a].at ("metric") != metric)
                continue;

            auto& row = rows.front();
            row.text = movementText (row.source, assertions[a].at ("rule"));
            row.status = severity (row.status) < severity ("assertion_failed") ? row.status : "assertion_failed";
            joined[a] = true;
        }

        problems.insert (problems.end(), rows.begin(), rows.end());
    }

    for (size_t a = 0; a < assertions.size(); ++a)
        if (! assertions[a].at ("passed").get<bool>() && ! joined[a])
            problems.push_back ({ "assertion_failed", assertions[a].at ("metric").get<std::string>(), showBrokenRule (assertions[a]), assertions[a] });

    std::stable_sort (problems.begin(), problems.end(), worse);
    return problems;
}

std::vector<ListedProblem> allProblems (const json& results)
{
    std::vector<ListedProblem> rows;

    for (const auto& scenario : results.at ("scenarios"))
        for (auto& problem : problemsOf (scenario))
            rows.push_back ({ scenario.at ("id").get<std::string>(), scenario.at ("id").get<std::string>(), std::move (problem) });

    std::stable_sort (rows.begin(), rows.end(), [] (const ListedProblem& a, const ListedProblem& b) { return worse (a.problem, b.problem); });

    // Matrix variants of one scenario, "robustness@bs=64" and "robustness@bs=512", whose
    // rows read the same share one.
    std::vector<ListedProblem> listed;
    std::vector<std::vector<std::string>> variants;

    for (auto& row : rows)
    {
        const auto at = row.link.find ('@');

        if (at != std::string::npos)
        {
            const auto same = std::find_if (listed.begin(), listed.end(), [&] (const ListedProblem& other)
            {
                return other.link.compare (0, at + 1, row.link, 0, at + 1) == 0 && other.problem.metric == row.problem.metric
                    && other.problem.status == row.problem.status && other.problem.text == row.problem.text;
            });

            if (same != listed.end())
            {
                variants[(size_t) (same - listed.begin())].push_back (row.link.substr (at + 1));
                continue;
            }
        }

        variants.push_back (at == std::string::npos ? std::vector<std::string>() : std::vector<std::string> { row.link.substr (at + 1) });
        listed.push_back (std::move (row));
    }

    for (size_t i = 0; i < listed.size(); ++i)
        if (variants[i].size() > 1)
            listed[i].scenarios = listed[i].link.substr (0, listed[i].link.find ('@')) + " (" + showVariants (variants[i]) + ")";

    return listed;
}

std::vector<ChannelCurve> curvesOf (const json& scenario, const std::string& group, const std::string& field)
{
    const auto metrics = scenario.value ("metrics", json::object());

    if (! metrics.contains (group))
        return {};

    // The tolerance of the comparison, or the field's default when nothing was compared.
    const auto& fields = metricCatalog().at (group);
    const auto info = std::find_if (fields.begin(), fields.end(), [&] (const FieldInfo& f) { return f.name == field; });
    json tolerance { { "abs", info != fields.end() ? info->tolerance : 0.0 }, { "rel", 0.0 } };

    std::vector<ChannelCurve> channels;
    const auto& perChannel = metrics[group].at ("per_channel");

    for (size_t c = 0; c < perChannel.size(); ++c)
    {
        ChannelCurve curve { { (int) c + 1 }, perChannel[c].value (field, json()), json(), "", tolerance };

        for (const auto& entry : scenario.value ("comparison", json::array()))
        {
            if (entry.at ("metric") != group + "." + field || entry.at ("channel") != (int) c + 1)
                continue;

            curve.baseline = entry.value ("baseline", json());
            curve.status = entry.at ("status").get<std::string>();
            curve.tolerance = tolerance = entry.at ("tolerance");
        }

        channels.push_back (std::move (curve));
    }

    const auto alike = [&] (const json& a, const json& b)
    {
        if (a.is_null() || b.is_null() || ! a.is_array() || ! b.is_array())
            return a == b;

        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
        {
            if (! a[i].is_number() || ! b[i].is_number())
            {
                if (a[i] != b[i])
                    return false;

                continue;
            }

            const auto x = a[i].get<double>(), y = b[i].get<double>();

            if (std::abs (x - y) > tolerance.value ("abs", 0.0) + tolerance.value ("rel", 0.0) * std::abs (x) + 1.0e-9)
                return false;
        }

        return true;
    };

    std::vector<ChannelCurve> curves;

    for (auto& channel : channels)
    {
        const auto same = std::find_if (curves.begin(), curves.end(), [&] (const ChannelCurve& curve)
        {
            return alike (curve.actual, channel.actual) && alike (curve.baseline, channel.baseline);
        });

        if (same == curves.end())
        {
            curves.push_back (std::move (channel));
            continue;
        }

        same->channels.push_back (channel.channels.front());

        if (severity (channel.status) < severity (same->status) || same->status.empty())
            same->status = channel.status;
    }

    return curves;
}

std::string showChannels (const std::vector<int>& channels)
{
    std::string text = channels.size() == 1 ? "channel " : "channels ";

    for (size_t i = 0; i < channels.size(); ++i)
        text += (i == 0 ? "" : i + 1 == channels.size() ? " and " : ", ") + std::to_string (channels[i]);

    return text;
}

std::string showCommit (const ReportInfo& info)
{
    if (info.commit.empty())
        return {};

    const auto isFullSha = info.commit.size() == 40 && std::all_of (info.commit.begin(), info.commit.end(), [] (char c) { return std::isxdigit ((unsigned char) c); });
    return "Commit " + (isFullSha ? info.commit.substr (0, 7) : info.commit) + (info.mergedInto.empty() ? "" : " (merged into " + info.mergedInto + ")");
}
} // namespace sonolint
