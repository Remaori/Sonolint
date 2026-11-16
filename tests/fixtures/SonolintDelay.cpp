// SonolintDelay: delays the audio by Delay samples and reports that latency to the
// host. With LieAboutLatency on it reports 10 samples too many, which Sonolint must
// catch as a latency mismatch.
#include "fixture.h"

class Delay : public Fixture
{
public:
    Delay() : delaySamples (addInt ("Delay", 0, 1000, 0)), lie (addBool ("LieAboutLatency")) {}

    void prepareToPlay (double, int) override
    {
        length = (size_t) delaySamples->get();
        position = 0;
        lines.assign ((size_t) getTotalNumInputChannels(), std::vector<float> (length, 0.0f));

        setLatencySamples ((int) length + (lie->get() ? 10 : 0));
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (length == 0)
            return;

        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            // Each line holds the last `length` samples: read the oldest, then put the new one in its place.
            for (size_t channel = 0; channel < lines.size(); ++channel)
                std::swap (lines[channel][position], buffer.getWritePointer ((int) channel)[n]);

            position = (position + 1) % length;
        }
    }

private:
    juce::AudioParameterInt* delaySamples;
    juce::AudioParameterBool* lie;
    std::vector<std::vector<float>> lines;
    size_t length = 0, position = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Delay(); }
