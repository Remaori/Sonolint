#pragma once

#include "core/runner/plugin_info.h"

#include <string>

namespace sonolint
{
// What `sonolint list-params` prints: a table for people, or JSON with --json.
std::string formatParamTable (const PluginInfo& plugin);
std::string formatParamJson (const PluginInfo& plugin);
} // namespace sonolint
