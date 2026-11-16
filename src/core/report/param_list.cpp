#include "core/report/param_list.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace sonolint
{
namespace
{
// Columns line up by characters, not bytes: parameter texts are UTF-8.
size_t displayWidth (const std::string& text)
{
    return (size_t) std::count_if (text.begin(), text.end(), [] (unsigned char c) { return (c & 0xC0) != 0x80; });
}

std::string threeDecimals (double value)
{
    std::ostringstream out;
    out.imbue (std::locale::classic());
    out << std::fixed << std::setprecision (3) << value;
    return out.str();
}
} // namespace

std::string formatParamTable (const PluginInfo& plugin)
{
    std::vector<std::vector<std::string>> rows;
    rows.push_back ({ "#", "Name", "ID", "Default", "Text at 0", "at 0.5", "at 1", "Steps", "Bypass" });

    for (const auto& p : plugin.params)
        rows.push_back ({ std::to_string (p.index), p.name, p.id, threeDecimals (p.defaultValue),
                          p.textAt0, p.textAtHalf, p.textAt1,
                          p.steps == 0 ? "continuous" : std::to_string (p.steps),
                          p.isBypass ? "yes" : "" });

    std::vector<size_t> widths (rows.front().size(), 0);

    for (const auto& row : rows)
        for (size_t column = 0; column < row.size(); ++column)
            widths[column] = std::max (widths[column], displayWidth (row[column]));

    auto text = plugin.name + " " + plugin.version + " (" + plugin.vendor + ")\n\n";

    for (const auto& row : rows)
    {
        std::string line;

        for (size_t column = 0; column < row.size(); ++column)
            line += row[column] + std::string (widths[column] - displayWidth (row[column]) + 2, ' ');

        line.erase (line.find_last_not_of (' ') + 1);   // no trailing spaces
        text += line + "\n";
    }

    return text;
}

std::string formatParamJson (const PluginInfo& plugin)
{
    auto params = nlohmann::json::array();

    for (const auto& p : plugin.params)
        params.push_back ({ { "index", p.index },
                            { "name", p.name },
                            { "id", p.id },
                            // The plugin gives a float: 0.6 arrives as 0.6000000238. Drop those digits.
                            { "default", std::round (p.defaultValue * 1.0e6) / 1.0e6 },
                            { "text", { { "0", p.textAt0 }, { "0.5", p.textAtHalf }, { "1", p.textAt1 } } },
                            { "steps", p.steps },
                            { "is_bypass", p.isBypass } });

    const nlohmann::json root { { "name", plugin.name },
                                { "vendor", plugin.vendor },
                                { "version", plugin.version },
                                { "parameters", params } };
    return root.dump (2) + "\n";
}
} // namespace sonolint
