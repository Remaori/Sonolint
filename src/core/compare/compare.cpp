#include "core/compare/compare.h"

#include "core/compare/catalog.h"
#include "core/config/text_match.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

// Room for the rounding of the stored values, far below any tolerance.
constexpr double kRoundingSlack = 1.0e-9;

int severity (const std::string& status)
{
    if (status == "error")             return 6;
    if (status == "regressed")         return 5;
    if (status == "changed")           return 5;
    if (status == "assertion_failed")  return 4;
    if (status == "removed")           return 3;
    if (status == "improved")          return 2;
    if (status == "new")               return 1;
    return 0;
}

std::string worse (const std::string& a, const std::string& b) { return severity (b) > severity (a) ? b : a; }

struct Rule
{
    double abs = 0.0, rel = 0.0;
    bool reportOnly = false;
};

// The entry that applies to `path`: the path itself, or else the longest glob that matches it.
const Tolerance* matching (const std::map<std::string, Tolerance>& tolerances, const std::string& path)
{
    if (const auto exact = tolerances.find (path); exact != tolerances.end())
        return &exact->second;

    const std::pair<const std::string, Tolerance>* best = nullptr;

    for (const auto& entry : tolerances)
        if (globMatch (entry.first, path) && (best == nullptr || entry.first.size() > best->first.size()))
            best = &entry;

    return best != nullptr ? &best->second : nullptr;
}

// The default of the field, then what the config says for every scenario, then what
// this scenario says. Each one replaces only what it sets.
Rule ruleFor (const FieldInfo& field, const std::string& path, const std::map<std::string, Tolerance>& global,
              const std::map<std::string, Tolerance>& own)
{
    Rule rule { field.tolerance, 0.0, field.direction == Direction::report };

    for (const auto* entry : { matching (global, path), matching (own, path) })
    {
        if (entry == nullptr)
            continue;

        rule.abs = entry->abs.value_or (rule.abs);
        rule.rel = entry->rel.value_or (rule.rel);
        rule.reportOnly = rule.reportOnly || entry->reportOnly;
    }

    return rule;
}

// One value against the baseline: its status and how far it moved.
std::pair<std::string, double> comparePoint (const json& actual, const json& baseline, const FieldInfo& field, const Rule& rule)
{
    if (! actual.is_number() || ! baseline.is_number())
        return { actual == baseline ? "pass" : "changed", 0.0 };   // yes/no, or a harmonic out of band

    auto a = actual.get<double>(), b = baseline.get<double>();

    if (field.unit == Unit::db)
    {
        a = std::max (a, kFloorDb);
        b = std::max (b, kFloorDb);
    }

    // Phases of 179 and -179 degrees are 2 degrees apart, not 358.
    const auto delta = field.unit == Unit::degrees ? std::remainder (a - b, 360.0) : a - b;

    if (std::abs (delta) <= rule.abs + rule.rel * std::abs (b) + kRoundingSlack)
        return { "pass", delta };

    if (field.direction == Direction::lower)
        return { delta < 0.0 ? "improved" : "regressed", delta };

    return { "changed", delta };
}

// A value or a curve against the baseline. Fills the status and the numbers of `entry`.
void compareValue (json& entry, const json& actual, const json& baseline, const FieldInfo& field, const Rule& rule)
{
    if (! actual.is_array() && ! baseline.is_array())
    {
        const auto [status, delta] = comparePoint (actual, baseline, field, rule);
        entry["status"] = status;

        if (actual.is_number() && baseline.is_number())
            entry["delta"] = delta;

        return;
    }

    // Curves go point by point; a different number of points is a change in itself.
    if (! actual.is_array() || ! baseline.is_array() || actual.size() != baseline.size())
    {
        entry["status"] = "changed";
        return;
    }

    std::string status = "pass";
    auto outside = 0;
    double largest = 0.0;

    for (size_t i = 0; i < actual.size(); ++i)
    {
        const auto [pointStatus, delta] = comparePoint (actual[i], baseline[i], field, rule);
        status = worse (status, pointStatus);
        outside += pointStatus == "pass" ? 0 : 1;

        if (std::abs (delta) > std::abs (largest))
            largest = delta;
    }

    entry["status"] = status;
    entry["delta"] = largest;   // of the point that moved the most
    entry["points_outside"] = outside;
}

// Every field of every metric group, on every channel, against the baseline entry of
// the scenario (null if it has none). Returns the comparison list.
json compareScenario (const json& metrics, const json& baseline, const std::map<std::string, Tolerance>& global,
                      const std::map<std::string, Tolerance>& own)
{
    std::set<std::string> groups;

    for (const auto& [group, values] : metrics.items())
        groups.insert (group);

    if (baseline.is_object())
        for (const auto& [group, values] : baseline.items())
            groups.insert (group);

    auto comparison = json::array();

    for (const auto& group : groups)
    {
        // cpu depends on the machine and is never compared; a group this version doesn't know is skipped.
        if (group == "cpu" || metricCatalog().count (group) == 0)
            continue;

        const auto actualChannels = metrics.contains (group) ? metrics[group].value ("per_channel", json::array()) : json::array();
        const auto storedChannels = baseline.is_object() && baseline.contains (group) ? baseline[group] : json::array();
        const auto numChannels = std::max (actualChannels.size(), storedChannels.size());

        for (const auto& field : metricCatalog().at (group))
        {
            const auto path = group + "." + field.name;
            const auto rule = ruleFor (field, path, global, own);

            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                const auto has = [&] (const json& channels) { return channel < channels.size() && channels[channel].contains (field.name); };

                if (! has (actualChannels) && ! has (storedChannels))
                    continue;

                json entry { { "metric", path }, { "channel", channel + 1 }, { "tolerance", { { "abs", rule.abs }, { "rel", rule.rel } } } };

                if (rule.reportOnly)
                    entry["report_only"] = true;

                if (! has (storedChannels))
                {
                    entry["status"] = "new";
                    entry["actual"] = roundForBaseline (actualChannels[channel][field.name], field.unit);
                }
                else if (! has (actualChannels))
                {
                    entry["status"] = "removed";
                    entry["baseline"] = storedChannels[channel][field.name];
                }
                else
                {
                    // Rounded like the baseline, so a run that matches it compares equal.
                    const auto actual = roundForBaseline (actualChannels[channel][field.name], field.unit);
                    const auto& stored = storedChannels[channel][field.name];
                    compareValue (entry, actual, stored, field, rule);
                    entry["actual"] = actual;
                    entry["baseline"] = stored;
                }

                comparison.push_back (std::move (entry));
            }
        }
    }

    return comparison;
}
} // namespace

void compareWithBaseline (json& results, const json& section, const std::vector<Scenario>& scenarios,
                          const std::map<std::string, Tolerance>& tolerances, const std::vector<std::string>& filters)
{
    std::set<std::string> ran;

    for (auto& scenario : results.at ("scenarios"))
    {
        const auto id = scenario.at ("id").get<std::string>();
        ran.insert (id);

        if (scenario.at ("status") == "error")
            continue;

        static const std::map<std::string, Tolerance> none;
        const auto config = std::find_if (scenarios.begin(), scenarios.end(), [&] (const Scenario& s) { return s.id == id; });
        const auto& own = config != scenarios.end() ? config->tolerances : none;
        const auto stored = section.is_object() && section.contains (id) ? section[id] : json();

        auto status = scenario.at ("status").get<std::string>();
        scenario["comparison"] = compareScenario (scenario.at ("metrics"), stored, tolerances, own);

        for (const auto& entry : scenario["comparison"])
            if (! entry.value ("report_only", false))
                status = worse (status, entry.at ("status").get<std::string>());

        scenario["status"] = status;
    }

    if (section.is_object())
        for (const auto& [id, stored] : section.items())
            if (ran.count (id) == 0
                && (filters.empty() || std::any_of (filters.begin(), filters.end(), [&] (const std::string& glob) { return globMatch (glob, id); })))
                results["scenarios"].push_back ({ { "id", id }, { "status", "removed" } });

    results["summary"] = summarise (results["scenarios"]);
}

const std::vector<std::string>& statusesWorstFirst()
{
    static const std::vector<std::string> statuses { "error", "regressed", "changed", "assertion_failed", "removed", "improved", "new", "pass" };
    return statuses;
}

json summarise (const json& scenarios)
{
    json summary = json::object();

    for (const auto& status : statusesWorstFirst())
        summary[status] = 0;

    for (const auto& scenario : scenarios)
        summary[scenario.at ("status").get<std::string>()] = summary[scenario.at ("status").get<std::string>()].get<int>() + 1;

    return summary;
}

bool fails (const json& results, FailOn policy)
{
    if (policy == FailOn::none)
        return false;

    for (const auto& scenario : results.at ("scenarios"))
    {
        const auto status = scenario.at ("status").get<std::string>();

        if (status == "error" || (policy == FailOn::regressions && severity (status) >= severity ("assertion_failed")))
            return true;

        // The status shows the worst problem only: a failed rule can hide behind a changed value.
        for (const auto& assertion : scenario.value ("assertions", json::array()))
            if (! assertion.at ("passed").get<bool>())
                return true;
    }

    return false;
}
} // namespace sonolint
