#include "host/plugin_loader.h"

#include "core/error.h"
#include "core/io/path_text.h"
#include "host/juce_plugin.h"

namespace sonolint
{
namespace
{
// A plugin instance needs a sample rate and a block size to be created. Reading its
// parameters doesn't depend on them.
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr int    kMaxTextLength = 128;
} // namespace

LoadedPlugin loadPlugin (const std::filesystem::path& path, const std::string& pluginName, double sampleRate, int blockSize)
{
    const auto pathText = toUtf8 (path);

    if (! std::filesystem::exists (path))
        throw Error (ErrorCode::pluginLoad, "plugin not found: " + pathText);

    // The headless format was only tried with Heft. If a third-party plugin doesn't load,
    // try juce::VST3PluginFormat (module juce_audio_processors) first: docs/adr/0016.
    LoadedPlugin plugin;
    plugin.formats = std::make_unique<juce::AudioPluginFormatManager>();
    plugin.formats->addFormat (std::make_unique<juce::VST3PluginFormatHeadless>());

    juce::OwnedArray<juce::PluginDescription> types;
    plugin.formats->getFormat (0)->findAllTypesForFile (types, juce::String::fromUTF8 (pathText.data(), (int) pathText.size()));

    if (types.isEmpty())
        throw Error (ErrorCode::pluginLoad, "not a VST3 plugin, or it could not be read: " + pathText);

    juce::StringArray names;

    for (const auto* type : types)
        names.add (type->name);

    const auto available = " (it contains: " + names.joinIntoString (", ").toStdString() + ")";
    auto chosen = 0;

    if (! pluginName.empty())
    {
        chosen = names.indexOf (juce::String::fromUTF8 (pluginName.data(), (int) pluginName.size()));

        if (chosen < 0)
            throw Error (ErrorCode::pluginLoad, pathText + " has no plugin called \"" + pluginName + "\"" + available);
    }
    else if (types.size() > 1)
    {
        throw Error (ErrorCode::pluginLoad, pathText + " holds more than one plugin; choose one with plugin.name" + available);
    }

    plugin.description = *types[chosen];

    juce::String error;
    plugin.instance = plugin.formats->createPluginInstance (plugin.description, sampleRate, blockSize, error);

    if (plugin.instance == nullptr)
        throw Error (ErrorCode::pluginLoad, "could not create an instance of " + pathText + ": " + error.toStdString());

    return plugin;
}

PluginInfo describePlugin (const LoadedPlugin& plugin)
{
    PluginInfo info;
    info.name    = plugin.description.name.toStdString();
    info.vendor  = plugin.description.manufacturerName.toStdString();
    info.version = plugin.description.version.toStdString();

    const auto* bypass = plugin.instance->getBypassParameter();

    for (auto* param : plugin.instance->getParameters())
    {
        ParamInfo p;
        p.index = param->getParameterIndex();
        p.name  = param->getName (kMaxTextLength).toStdString();

        if (const auto* hosted = plugin.instance->getHostedParameter (p.index))
            p.id = hosted->getParameterID().toStdString();

        p.defaultValue = param->getDefaultValue();
        p.textAt0      = param->getText (0.0f, kMaxTextLength).toStdString();
        p.textAtHalf   = param->getText (0.5f, kMaxTextLength).toStdString();
        p.textAt1      = param->getText (1.0f, kMaxTextLength).toStdString();

        // JUCE reports a continuous parameter as "the default number of steps".
        const auto steps = param->getNumSteps();
        p.steps    = steps == juce::AudioProcessor::getDefaultNumParameterSteps() ? 0 : steps;
        p.isBypass = param == bypass;

        info.params.push_back (std::move (p));
    }

    return info;
}

PluginInfo inspectPlugin (const std::filesystem::path& path, const std::string& pluginName)
{
    return describePlugin (loadPlugin (path, pluginName, kSampleRate, kBlockSize));
}
} // namespace sonolint
