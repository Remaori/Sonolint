// SonolintFilter: a first-order low-pass at Cutoff (bilinear transform, cutoff
// pre-warped): y[n] = b * (x[n] + x[n - 1]) - a * y[n - 1]. Its frequency response
// has a formula, which what Sonolint measures must match.
#include "fixture.h"

class Filter : public Fixture
{
public:
    Filter() : cutoffHz (addFloat ("Cutoff", 20.0f, 20000.0f, 1000.0f, "Hz")) {}

    void prepareToPlay (double sampleRate, int) override
    {
        const auto k = std::tan (juce::MathConstants<double>::pi * (double) cutoffHz->get() / sampleRate);
        b = k / (1.0 + k);
        a = (k - 1.0) / (k + 1.0);
        states.assign ((size_t) getTotalNumInputChannels(), {});
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        for (size_t channel = 0; channel < states.size(); ++channel)
        {
            auto& state = states[channel];
            auto* samples = buffer.getWritePointer ((int) channel);

            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                const auto x = (double) samples[n];
                const auto y = b * (x + state.previousInput) - a * state.previousOutput;
                state.previousInput = x;
                state.previousOutput = y;
                samples[n] = (float) y;
            }
        }
    }

private:
    struct State
    {
        double previousInput = 0.0, previousOutput = 0.0;
    };

    juce::AudioParameterFloat* cutoffHz;
    std::vector<State> states;
    double a = 0.0, b = 0.0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Filter(); }
