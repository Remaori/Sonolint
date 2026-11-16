// SonolintGain: y = g * x. Nothing else: no latency, no smoothing, no state.
// Sonolint must measure exactly the gain that was set, no distortion and no latency.
#include "fixture.h"

class Gain : public Fixture
{
public:
    Gain() : gainDb (addFloat ("Gain", -24.0f, 12.0f, 0.0f, "dB")) {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (juce::Decibels::decibelsToGain (gainDb->get()));
    }

private:
    juce::AudioParameterFloat* gainDb;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Gain(); }
