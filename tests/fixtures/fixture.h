#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// What the test plugins share: an effect with one bus in and one out, mono or stereo,
// with no editor, no programs and no saved state. Each plugin adds its parameters and
// its processBlock.
class Fixture : public juce::AudioProcessor
{
public:
    Fixture()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto main = layouts.getMainOutputChannelSet();
        return main == layouts.getMainInputChannelSet() && (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

protected:
    // Each one adds a parameter called `name` and returns it, to read while processing.
    juce::AudioParameterFloat* addFloat (const juce::String& name, float low, float high, float initial, const juce::String& unit = {})
    {
        auto* param = new juce::AudioParameterFloat (id (name), name, juce::NormalisableRange<float> (low, high), initial,
                                                     juce::AudioParameterFloatAttributes().withLabel (unit));
        addParameter (param);
        return param;
    }

    juce::AudioParameterInt* addInt (const juce::String& name, int low, int high, int initial)
    {
        auto* param = new juce::AudioParameterInt (id (name), name, low, high, initial);
        addParameter (param);
        return param;
    }

    juce::AudioParameterBool* addBool (const juce::String& name)
    {
        auto* param = new juce::AudioParameterBool (id (name), name, false);
        addParameter (param);
        return param;
    }

    juce::AudioParameterChoice* addChoice (const juce::String& name, const juce::StringArray& choices)
    {
        auto* param = new juce::AudioParameterChoice (id (name), name, choices, 0);
        addParameter (param);
        return param;
    }

private:
    static juce::ParameterID id (const juce::String& name) { return { name.toLowerCase(), 1 }; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fixture)
};
