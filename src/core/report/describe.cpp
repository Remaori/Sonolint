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

std::string showMovement (const json& entry)
{
    const auto& actual = entry.at ("actual");
    const auto& baseline = entry.at ("baseline");

    if (actual.is_array() && baseline.is_array() && actual.size() != baseline.size())
        return std::to_string (actual.size()) + " points, and the baseline has " + std::to_string (baseline.size());

    if (actual.is_array())
        return std::to_string (entry.at ("points_outside").get<int>()) + " of " + std::to_string (actual.size()) + " points moved, the most by "
             + showSigned (entry.at ("delta").get<double>()) + "; tolerance " + showTolerance (entry.at ("tolerance"));

    auto text = showValue (actual) + ", was " + showValue (baseline);

    if (entry.contains ("delta"))
        text += " (" + showSigned (entry["delta"].get<double>()) + "; tolerance " + showTolerance (entry.at ("tolerance")) + ")";

    return text;
}

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
    if (rule.contains ("approx"))   return showValue (rule["approx"]) + " within " + showValue (rule["tol"]);
    if (rule.contains ("min"))      return "at least " + showValue (rule["min"]);
    if (rule.contains ("max"))      return "at most " + showValue (rule["max"]);
    if (rule.contains ("between"))  return "from " + showValue (rule["between"][0]) + " to " + showValue (rule["between"][1]);

    return showValue (rule.at ("equals"));
}

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

    for (const auto& assertion : scenario.value ("assertions", json::array()))
    {
        if (assertion.at ("passed").get<bool>())
            continue;

        const auto& actual = assertion.at ("actual");
        problems.push_back ({ "assertion_failed", assertion.at ("metric").get<std::string>(),
                              (actual.is_array() ? "has points outside" : "is " + showValue (actual)) + ", expected " + showRule (assertion.at ("rule")),
                              assertion });
    }

    for (const auto& entry : scenario.value ("comparison", json::array()))
    {
        const auto moved = entry.at ("status").get<std::string>();

        if (entry.value ("report_only", false))
            continue;   // shown in full in the HTML report; it never changes the status

        if (moved == "regressed" || moved == "changed" || moved == "improved")
            problems.push_back ({ moved, metricLabel (entry), showMovement (entry), entry });
        else if (moved == "removed")
            problems.push_back ({ moved, metricLabel (entry), "in the baseline, but no longer measured", entry });
    }

    std::stable_sort (problems.begin(), problems.end(), [] (const Problem& a, const Problem& b) { return severity (a.status) < severity (b.status); });
    return problems;
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
