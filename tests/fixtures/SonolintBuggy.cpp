// SonolintBuggy: a plugin that passes the audio through, with four bugs that can be
// switched on one at a time. They are simulated on purpose (no real undefined
// behaviour), and each one must make exactly its own Sonolint check fail:
//
//   Nondeterministic  noise from a random seed at -100 dBFS    -> determinism
//   BlockBug          a gain ramp that restarts at every block -> block invariance
//   NanAbove90        NaN in the output when Drive is over 0.9  -> the NaN check
//   Oscillate         a 1 kHz tone at -40 dBFS, also in silence -> noise floor
#include "fixture.h"

#include <random>

class Buggy : public Fixture
{
public:
    Buggy()
        : drive (addFloat ("Drive", 0.0f, 1.0f, 0.5f)),
          nondeterministic (addBool ("Nondeterministic")),
          blockBug (addBool ("BlockBug")),
          nanAbove90 (addBool ("NanAbove90")),
          oscillate (addBool ("Oscillate"))
    {
    }

    void prepareToPlay (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        phase = 0.0;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto numSamples = buffer.getNumSamples();
        const auto phaseStep = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            for (int n = 0; n < numSamples; ++n)
            {
                if (blockBug->get())
                    samples[n] *= 1.0f - 0.01f * (float) n / (float) numSamples;

                if (nondeterministic->get())
                    samples[n] += 1.0e-5f * (2.0f * random.nextFloat() - 1.0f);

                if (oscillate->get())
                    samples[n] += 0.01f * (float) std::sin (phase + phaseStep * n);

                if (nanAbove90->get() && drive->get() > 0.9f)
                    samples[n] = std::numeric_limits<float>::quiet_NaN();
            }
        }

        phase = std::fmod (phase + phaseStep * numSamples, juce::MathConstants<double>::twoPi);
    }

private:
    juce::AudioParameterFloat* drive;
    juce::AudioParameterBool *nondeterministic, *blockBug, *nanAbove90, *oscillate;

    juce::Random random { (juce::int64) std::random_device {}() };   // a different seed in every instance
    double sampleRate = 48000.0, phase = 0.0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Buggy(); }
