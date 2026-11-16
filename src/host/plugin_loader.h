#pragma once

#include "core/runner/plugin_info.h"

#include <filesystem>

namespace sonolint
{
// Loads the VST3 at `path` the way a host would, reads its name and parameters and
// unloads it. Call it on the main thread, with JUCE initialised.
// Throws Error (ErrorCode::pluginLoad) if the plugin can't be loaded.
PluginInfo inspectPlugin (const std::filesystem::path& path);
} // namespace sonolint
