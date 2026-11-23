#pragma once

#include "core/runner/renderer.h"

#include <filesystem>
#include <string>

namespace sonolint
{
// The Renderer that hosts a real VST3 with JUCE, the way a DAW would. Every render
// creates a fresh instance of the plugin and destroys it at the end.
// Use it on the main thread, with JUCE initialised.
class Vst3Renderer : public Renderer
{
public:
    explicit Vst3Renderer (std::filesystem::path pluginPathToUse, std::string pluginNameToUse = {});

    // Throws Error (pluginLoad) if the plugin can't be loaded, and Error (config) for a
    // channel layout it doesn't accept or a bad parameter or state file.
    RenderResult render (const RenderRequest& request) override;

private:
    std::filesystem::path pluginPath;
    std::string pluginName;
};
} // namespace sonolint
