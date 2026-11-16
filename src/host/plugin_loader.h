#pragma once

#include "core/runner/plugin_info.h"

#include <filesystem>
#include <string>

namespace sonolint
{
// Loads the VST3 at `path` the way a host would, reads its name and parameters and
// unloads it. When the file holds several plugins, pluginName says which one.
// Call it on the main thread, with JUCE initialised.
// Throws Error (ErrorCode::pluginLoad) if the plugin can't be loaded.
PluginInfo inspectPlugin (const std::filesystem::path& path, const std::string& pluginName = {});
} // namespace sonolint
