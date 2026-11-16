#pragma once

#include "core/runner/plugin_info.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <filesystem>
#include <memory>
#include <string>

namespace sonolint
{
// A plugin instance together with the format object that created it, which has to
// outlive it: members are destroyed from the last one up.
struct LoadedPlugin
{
    std::unique_ptr<juce::AudioPluginFormatManager> formats;
    juce::PluginDescription description;
    std::unique_ptr<juce::AudioPluginInstance> instance;
};

// Creates a fresh instance of the VST3 at `path`. When the file holds several plugins,
// pluginName says which one. Throws Error (pluginLoad) if it can't.
LoadedPlugin loadPlugin (const std::filesystem::path& path, const std::string& pluginName, double sampleRate, int blockSize);

PluginInfo describePlugin (const LoadedPlugin& plugin);
} // namespace sonolint
