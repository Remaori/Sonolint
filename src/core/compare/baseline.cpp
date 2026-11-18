#include "core/compare/baseline.h"

#include "core/compare/catalog.h"
#include "core/config/text_match.h"
#include "core/error.h"
#include "core/io/path_text.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

constexpr auto kSchema = "sonolint.baseline/v1";

// What the baseline keeps of a scenario's metrics.
json storedMetrics (const json& metrics)
{
    json stored = json::object();

    for (const auto& [group, values] : metrics.items())
    {
        if (group == "cpu")
            continue;   // it depends on the machine and its load: it would change in every diff

        auto channels = json::array();

        for (const auto& channel : values.at ("per_channel"))
        {
            json fields = json::object();

            for (const auto& field : metricCatalog().at (group))
                if (channel.contains (field.name))
                    fields[field.name] = roundForBaseline (channel[field.name], field.unit);

            channels.push_back (std::move (fields));
        }

        stored[group] = std::move (channels);
    }

    return stored;
}
} // namespace

json updateBaseline (const json& existing, const json& results, const std::string& platform, const std::vector<std::string>& filters,
                     const std::string& sonolintVersion)
{
    auto baseline = existing.is_object() ? existing : json::object();
    auto& scenarios = baseline["platforms"][platform]["scenarios"];

    if (! scenarios.is_object())
        scenarios = json::object();

    std::set<std::string> errored;

    for (const auto& scenario : results.at ("scenarios"))
        if (scenario.at ("status") == "error")
            errored.insert (scenario.at ("id").get<std::string>());

    // What stays as it was: scenarios that ended in error and, with filters, everything outside them.
    json updated = json::object();

    for (const auto& [id, stored] : scenarios.items())
        if (errored.count (id) > 0
            || (! filters.empty() && std::none_of (filters.begin(), filters.end(), [&] (const std::string& glob) { return globMatch (glob, id); })))
            updated[id] = stored;

    for (const auto& scenario : results.at ("scenarios"))
        if (scenario.at ("status") != "error")
            updated[scenario.at ("id").get<std::string>()] = storedMetrics (scenario.at ("metrics"));

    scenarios = std::move (updated);
    baseline["plugin"] = { { "name", results.at ("plugin").at ("name") }, { "version", results.at ("plugin").at ("version") } };
    baseline["schema"] = kSchema;
    baseline["sonolint_version"] = sonolintVersion;
    return baseline;
}

json platformScenarios (const json& baseline, const std::string& platform)
{
    if (! baseline.is_object() || ! baseline.contains ("platforms") || ! baseline["platforms"].contains (platform))
        return {};

    return baseline["platforms"][platform].value ("scenarios", json());
}

std::string formatBaseline (const json& baseline)
{
    return baseline.dump (2) + "\n";   // objects keep their keys sorted, so the order is alphabetical
}

json loadBaseline (const std::filesystem::path& file)
{
    if (! std::filesystem::exists (file))
        return {};

    std::ifstream stream (file, std::ios::binary);
    std::ostringstream content;
    content << stream.rdbuf();

    const auto baseline = json::parse (content.str(), nullptr, false);

    if (! stream || baseline.is_discarded() || ! baseline.is_object() || baseline.value ("schema", "") != kSchema)
        throw Error (ErrorCode::config, toUtf8 (file) + " is not a Sonolint baseline (schema " + kSchema
                                            + "); fix it, or delete it and run sonolint baseline update");

    return baseline;
}

void saveBaseline (const std::filesystem::path& file, const json& baseline)
{
    if (file.has_parent_path())
        std::filesystem::create_directories (file.parent_path());

    std::ofstream stream (file, std::ios::binary);   // binary: LF line endings on Windows too
    stream << formatBaseline (baseline);

    if (! stream)
        throw Error (ErrorCode::config, "could not write the baseline " + toUtf8 (file));
}
} // namespace sonolint
