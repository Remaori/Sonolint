#include "host/vst3_renderer.h"

#include "core/error.h"
#include "core/io/path_text.h"
#include "core/signals/prng.h"
#include "host/juce_plugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

namespace sonolint
{
namespace
{
std::string layoutName (int channels) { return channels == 1 ? "mono" : "stereo"; }

// The main bus gets `channels` in and out, and every other bus (sidechains, extra
// outputs) is switched off.
void useMainBusOnly (juce::AudioPluginInstance& instance, int channels)
{
    if (instance.getBusCount (true) == 0 || instance.getBusCount (false) == 0)
        throw Error (ErrorCode::pluginLoad, "the plugin has no audio input or no audio output; Sonolint measures effects only");

    const auto layoutWith = [&] (int count)
    {
        auto layout = instance.getBusesLayout();

        for (auto* buses : { &layout.inputBuses, &layout.outputBuses })
            for (int bus = 0; bus < buses->size(); ++bus)
                buses->getReference (bus) = bus == 0 ? juce::AudioChannelSet::canonicalChannelSet (count) : juce::AudioChannelSet::disabled();

        return layout;
    };

    if (instance.setBusesLayout (layoutWith (channels)))
        return;

    juce::StringArray accepted;

    for (const auto count : { 1, 2 })
        if (instance.checkBusesLayoutSupported (layoutWith (count)))
            accepted.add (layoutName (count));

    // Only this scenario's problem: the others may run with another layout.
    throw Error (ErrorCode::config, "the plugin doesn't accept " + layoutName (channels) + " on its main bus; it accepts: "
                                        + (accepted.isEmpty() ? "neither mono nor stereo" : accepted.joinIntoString (", ").toStdString()));
}

void loadState (juce::AudioPluginInstance& instance, const std::filesystem::path& file)
{
    std::ifstream stream (file, std::ios::binary);

    if (! stream)
        throw Error (ErrorCode::config, "could not read the state file " + toUtf8 (file));

    const std::vector<char> bytes ((std::istreambuf_iterator<char> (stream)), std::istreambuf_iterator<char>());

    // JUCE ignores what it can't read, and the scenario would measure the plugin's defaults.
    if (juce::AudioProcessor::getXmlFromBinary (bytes.data(), (int) bytes.size()) == nullptr)
        throw Error (ErrorCode::config, toUtf8 (file) + " is not a state saved by a JUCE host; .vstpreset files can't be read yet");

    instance.setStateInformation (bytes.data(), (int) bytes.size());
}

// Whether two texts of a parameter say the same: "on" and "On", or "12 dB" and "12.0".
bool sameText (const juce::String& asked, const juce::String& shown)
{
    const auto a = asked.trim(), b = shown.trim();

    if (a.equalsIgnoreCase (b))
        return true;

    const auto startsWithNumber = [] (const juce::String& text) { return juce::CharacterFunctions::isDigit (text.trimCharactersAtStart ("+-.")[0]); };

    if (! startsWithNumber (a) || ! startsWithNumber (b))
        return false;

    // The plugin shows its value with some decimals: half of the last one is rounding.
    const auto decimals = b.fromFirstOccurrenceOf (".", false, false).initialSectionContainingOnly ("0123456789").length();
    const auto rounding = std::max (0.5 * std::pow (10.0, -decimals), 1.0e-3 * std::abs (a.getDoubleValue()));
    return std::abs (a.getDoubleValue() - b.getDoubleValue()) <= rounding + 1.0e-9;
}

// Sets a parameter from text in the plugin's own units. The plugin reads whatever it
// can, so what it understood is read back: in a list of choices a text it doesn't have
// is an error; elsewhere, where the same value can be written in many ways, a warning.
void setText (juce::AudioProcessorParameter& parameter, const std::string& name, const std::string& text, std::vector<std::string>& warnings)
{
    constexpr int maxLength = 128;
    const auto asked = juce::String::fromUTF8 (text.data(), (int) text.size());
    parameter.setValueNotifyingHost (parameter.getValueForText (asked));
    const auto shown = parameter.getText (parameter.getValue(), maxLength);

    if (sameText (asked, shown))
        return;

    const auto problem = "the plugin read " + name + " \"" + text + "\" as \"" + shown.toStdString() + "\"";

    if (! parameter.isDiscrete())
    {
        warnings.push_back (problem);
        return;
    }

    constexpr int mostChoices = 20;
    const auto steps = parameter.getNumSteps();
    juce::StringArray choices;

    for (int step = 0; step < std::min (steps, mostChoices); ++step)
        choices.add (parameter.getText ((float) step / (float) std::max (1, steps - 1), maxLength));

    throw Error (ErrorCode::config, problem + "; it takes: " + choices.joinIntoString (", ").toStdString() + (steps > mostChoices ? ", ..." : ""));
}

void setParams (juce::AudioPluginInstance& instance, const PluginInfo& info, const std::vector<ParamValue>& params, std::vector<std::string>& warnings)
{
    for (const auto& param : params)
    {
        auto* parameter = instance.getParameters()[(int) findParam (info, param.key)];

        if (const auto* normalized = std::get_if<double> (&param.value))
            parameter->setValueNotifyingHost ((float) *normalized);
        else
            setText (*parameter, param.key, std::get<std::string> (param.value), warnings);
    }
}
} // namespace

Vst3Renderer::Vst3Renderer (std::filesystem::path pluginPathToUse, std::string pluginNameToUse)
    : pluginPath (std::move (pluginPathToUse)), pluginName (std::move (pluginNameToUse))
{
}

RenderResult Vst3Renderer::render (const RenderRequest& request)
{
    const auto maxBlock = request.blocks.maxBlockSize;
    const auto channels = request.channels;

    // The order is the one a DAW follows: layout, real time or not, state, parameters,
    // prepare, and only then audio.
    const auto plugin = loadPlugin (pluginPath, pluginName, request.sampleRate, maxBlock);
    auto& instance = *plugin.instance;

    useMainBusOnly (instance, channels);
    instance.setNonRealtime (request.nonRealtime);

    if (request.stateFile)
        loadState (instance, *request.stateFile);

    RenderResult result;
    result.info = describePlugin (plugin);
    setParams (instance, result.info, request.params, result.warnings);

    instance.prepareToPlay (request.sampleRate, maxBlock);

    const juce::ScopedNoDenormals noDenormals;
    juce::AudioBuffer<float> buffer (channels, maxBlock);
    juce::MidiBuffer midi;

    // Warm-up: silence that is processed and thrown away, so parameter smoothing settles.
    for (auto remaining = request.warmupSamples; remaining > 0;)
    {
        const auto size = std::min (remaining, maxBlock);
        buffer.setSize (channels, size, false, false, true);
        buffer.clear();
        instance.processBlock (buffer, midi);
        midi.clear();
        remaining -= size;
    }

    result.reportedLatencySamples = instance.getLatencySamples();

    const auto inputSamples = request.input.numSamples();
    const auto totalSamples = inputSamples + (size_t) std::max (0, request.tailSamples);

    result.output.sampleRate = request.sampleRate;
    result.output.channels.assign ((size_t) channels, std::vector<float> (totalSamples, 0.0f));

    Prng blockSizes (request.blocks.randomSeed.value_or (0));

    for (size_t position = 0; position < totalSamples;)
    {
        auto size = (size_t) maxBlock;

        if (request.blocks.randomSeed)
            size = 1 + (size_t) (blockSizes.next() % (uint64_t) maxBlock);

        size = std::min (size, totalSamples - position);
        buffer.setSize (channels, (int) size, false, false, true);
        buffer.clear();

        // The input, and silence once it runs out (the tail).
        if (position < inputSamples)
            for (int channel = 0; channel < channels; ++channel)
                buffer.copyFrom (channel, 0, request.input.channels[(size_t) channel].data() + position,
                                 (int) std::min (size, inputSamples - position));

        const auto started = std::chrono::steady_clock::now();
        instance.processBlock (buffer, midi);
        result.processSecondsPerBlock.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count());
        midi.clear();

        for (int channel = 0; channel < channels; ++channel)
            std::copy_n (buffer.getReadPointer (channel), size, result.output.channels[(size_t) channel].begin() + (std::ptrdiff_t) position);

        position += size;
    }

    if (const auto latencyNow = instance.getLatencySamples(); latencyNow != result.reportedLatencySamples)
        result.warnings.push_back ("the plugin changed its latency during the render, from " + std::to_string (result.reportedLatencySamples)
                                   + " to " + std::to_string (latencyNow) + " samples");

    instance.releaseResources();
    return result;
}
} // namespace sonolint
