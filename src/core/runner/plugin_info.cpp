#include "core/runner/plugin_info.h"

#include "core/config/text_match.h"
#include "core/error.h"

#include <algorithm>

namespace sonolint
{
size_t findParam (const PluginInfo& info, const std::string& key)
{
    const auto named = [&] (const ParamInfo& param) { return param.name == key; };
    const auto count = std::count_if (info.params.begin(), info.params.end(), named);

    if (count > 1)
        throw Error (ErrorCode::config, "the plugin has " + std::to_string (count) + " parameters called \"" + key
                                            + "\"; use the ID instead (sonolint list-params shows it)");

    auto found = std::find_if (info.params.begin(), info.params.end(), named);

    if (found == info.params.end())
        found = std::find_if (info.params.begin(), info.params.end(), [&] (const ParamInfo& param) { return param.id == key; });

    if (found == info.params.end())
    {
        std::vector<std::string> names;

        for (const auto& param : info.params)
            names.push_back (param.name);

        throw Error (ErrorCode::config, "unknown parameter \"" + key + "\"" + didYouMean (key, names));
    }

    return (size_t) (found - info.params.begin());
}
} // namespace sonolint
