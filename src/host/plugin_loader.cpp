#include "host/plugin_loader.h"

#include "core/error.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

namespace sonolint
{
namespace
{
// A plugin instance needs a sample rate and a block size to be created. Reading its
// parameters doesn't depend on them.
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr int    kMaxTextLength = 128;

std::string toUtf8 (const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return { text.begin(), text.end() };
}
} // namespace

PluginInfo inspectPlugin (const std::filesystem::path& path)
{
    const auto pathText = toUtf8 (path);

    if (! std::filesystem::exists (path))
        throw Error (ErrorCode::pluginLoad, "plugin not found: " + pathText);

    // The headless format was only tried with Heft. If a third-party plugin doesn't load,
    // try juce::VST3PluginFormat (module juce_audio_processors) first: docs/adr/0016.
    juce::AudioPluginFormatManager manager;
    manager.addFormat (std::make_unique<juce::VST3PluginFormatHeadless>());

    juce::OwnedArray<juce::PluginDescription> types;
    manager.getFormat (0)->findAllTypesForFile (types, juce::String::fromUTF8 (pathText.data(), (int) pathText.size()));

    if (types.isEmpty())
        throw Error (ErrorCode::pluginLoad, "not a VST3 plugin, or it could not be read: " + pathText);

    if (types.size() > 1)
    {
        juce::StringArray names;

        for (const auto* type : types)
            names.add (type->name);

        throw Error (ErrorCode::pluginLoad, pathText + " contains more than one plugin (" + names.joinIntoString (", ").toStdString()
                                                + "); choosing one is not supported yet");
    }

    juce::String error;
    const auto plugin = manager.createPluginInstance (*types[0], kSampleRate, kBlockSize, error);

    if (plugin == nullptr)
        throw Error (ErrorCode::pluginLoad, "could not create an instance of " + pathText + ": " + error.toStdString());

    PluginInfo info;
    info.name    = types[0]->name.toStdString();
    info.vendor  = types[0]->manufacturerName.toStdString();
    info.version = types[0]->version.toStdString();

    const auto* bypass = plugin->getBypassParameter();

    for (auto* param : plugin->getParameters())
    {
        ParamInfo p;
        p.index = param->getParameterIndex();
        p.name  = param->getName (kMaxTextLength).toStdString();

        if (const auto* hosted = plugin->getHostedParameter (p.index))
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
} // namespace sonolint
