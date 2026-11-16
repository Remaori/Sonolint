#pragma once

#include <string>
#include <vector>

namespace sonolint
{
struct ParamInfo
{
    int index = 0;
    std::string name;
    std::string id;              // the hosted parameter ID; in VST3 usually a number as text
    double defaultValue = 0.0;   // normalized 0..1
    std::string textAt0, textAtHalf, textAt1;   // what the plugin shows at 0, 0.5 and 1
    int steps = 0;               // 0 = continuous
    bool isBypass = false;
};

struct PluginInfo
{
    std::string name, vendor, version;
    std::vector<ParamInfo> params;
};
} // namespace sonolint
