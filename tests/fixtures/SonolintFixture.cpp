// SonolintFixture: the test plugin. Behaviour chooses what it does, and each behaviour
// has a result known exactly, so what Sonolint measures on it can be checked:
//
//   Gain     y = g * x                                          Gain
//   Delay    a delay of N samples, reported to the host          Delay, LieAboutLatency (reports 10 more)
//   Clipper  a hard clip, at 1x or with 4x oversampling          Threshold, Oversample
//   Filter   a first-order low-pass                              Cutoff
//   Buggy    passes the audio through, with bugs to switch on    Drive and the four bugs below
//
// The parameters of the other behaviours do nothing. The host sets the parameters
// before prepareToPlay, which fixes the behaviour and the latency it reports.
//
// The bugs are simulated on purpose (no real undefined behaviour), and each one must
// make exactly its own check fail:
//
//   Nondeterministic  noise from a random seed at -100 dBFS      -> determinism
//   BlockBug          a gain ramp that restarts at every block   -> block invariance
//   NanAbove90        NaN in the output when Drive is over 0.9   -> the NaN check
//   Oscillate         a 1 kHz tone at -40 dBFS, also in silence  -> noise floor
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <random>

class SonolintFixture final : public juce::AudioProcessor
{
public:
    SonolintFixture()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          behaviour (addChoice ("Behaviour", { "Gain", "Delay", "Clipper", "Filter", "Buggy" })),
          gainDb (addFloat ("Gain", -24.0f, 12.0f, 0.0f, "dB")),
          delaySamples (addInt ("Delay", 0, 1000, 0)),
          lieAboutLatency (addBool ("LieAboutLatency")),
          thresholdDb (addFloat ("Threshold", -24.0f, 0.0f, -6.0f, "dB")),
          oversample (addChoice ("Oversample", { "1x", "4x" })),
          cutoffHz (addFloat ("Cutoff", 20.0f, 20000.0f, 1000.0f, "Hz")),
          drive (addFloat ("Drive", 0.0f, 1.0f, 0.5f)),
          nondeterministic (addBool ("Nondeterministic")),
          blockBug (addBool ("BlockBug")),
          nanAbove90 (addBool ("NanAbove90")),
          oscillate (addBool ("Oscillate"))
    {
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto main = layouts.getMainOutputChannelSet();
        return main == layouts.getMainInputChannelSet() && (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay (double newSampleRate, int largestBlock) override
    {
        sampleRate = newSampleRate;
        current = (Behaviour) behaviour->getIndex();

        const auto channels = (size_t) getTotalNumInputChannels();
        auto latency = 0;

        delayLength = 0;
        oversampling.reset();
        filterStates.assign (channels, {});
        phase = 0.0;

        if (current == delay)
        {
            delayLength = (size_t) delaySamples->get();
            delayPosition = 0;
            delayLines.assign (channels, std::vector<float> (delayLength, 0.0f));
            latency = (int) delayLength + (lieAboutLatency->get() ? 10 : 0);
        }
        else if (current == clipper && oversample->getIndex() == 1)
        {
            // 2 stages of 2x. The last argument asks for a latency that is a whole number of samples.
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (channels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                             true, true);
            oversampling->initProcessing ((size_t) largestBlock);
            latency = (int) std::lround (oversampling->getLatencyInSamples());
        }
        else if (current == filter)
        {
            // Bilinear transform with the cutoff pre-warped: y[n] = b * (x[n] + x[n - 1]) - a * y[n - 1].
            const auto k = std::tan (juce::MathConstants<double>::pi * (double) cutoffHz->get() / sampleRate);
            filterB = k / (1.0 + k);
            filterA = (k - 1.0) / (k + 1.0);
        }

        setLatencySamples (latency);
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        switch (current)
        {
            case gain:     buffer.applyGain (juce::Decibels::decibelsToGain (gainDb->get())); break;
            case delay:    processDelay (buffer); break;
            case clipper:  processClipper (buffer); break;
            case filter:   processFilter (buffer); break;
            case buggy:    processBuggy (buffer); break;
        }
    }

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

private:
    enum Behaviour { gain, delay, clipper, filter, buggy };

    void processDelay (juce::AudioBuffer<float>& buffer)
    {
        if (delayLength == 0)
            return;

        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            // Each line holds the last `delayLength` samples: read the oldest, then put the new one in its place.
            for (size_t channel = 0; channel < delayLines.size(); ++channel)
                std::swap (delayLines[channel][delayPosition], buffer.getWritePointer ((int) channel)[n]);

            delayPosition = (delayPosition + 1) % delayLength;
        }
    }

    void processClipper (juce::AudioBuffer<float>& buffer)
    {
        const auto threshold = juce::Decibels::decibelsToGain (thresholdDb->get());

        const auto clip = [threshold] (juce::dsp::AudioBlock<float> audio)
        {
            for (size_t channel = 0; channel < audio.getNumChannels(); ++channel)
                juce::FloatVectorOperations::clip (audio.getChannelPointer (channel), audio.getChannelPointer (channel), -threshold, threshold,
                                                   (int) audio.getNumSamples());
        };

        juce::dsp::AudioBlock<float> block (buffer);

        if (oversampling == nullptr)
        {
            clip (block);
            return;
        }

        clip (oversampling->processSamplesUp (block));
        oversampling->processSamplesDown (block);
    }

    void processFilter (juce::AudioBuffer<float>& buffer)
    {
        for (size_t channel = 0; channel < filterStates.size(); ++channel)
        {
            auto& state = filterStates[channel];
            auto* samples = buffer.getWritePointer ((int) channel);

            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                const auto x = (double) samples[n];
                const auto y = filterB * (x + state.previousInput) - filterA * state.previousOutput;
                state.previousInput = x;
                state.previousOutput = y;
                samples[n] = (float) y;
            }
        }
    }

    void processBuggy (juce::AudioBuffer<float>& buffer)
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

    static juce::ParameterID id (const juce::String& name) { return { name.toLowerCase(), 1 }; }

    struct FilterState
    {
        double previousInput = 0.0, previousOutput = 0.0;
    };

    juce::AudioParameterChoice* behaviour;
    juce::AudioParameterFloat* gainDb;
    juce::AudioParameterInt* delaySamples;
    juce::AudioParameterBool* lieAboutLatency;
    juce::AudioParameterFloat* thresholdDb;
    juce::AudioParameterChoice* oversample;
    juce::AudioParameterFloat* cutoffHz;
    juce::AudioParameterFloat* drive;
    juce::AudioParameterBool *nondeterministic, *blockBug, *nanAbove90, *oscillate;

    Behaviour current = gain;
    double sampleRate = 48000.0;

    std::vector<std::vector<float>> delayLines;
    size_t delayLength = 0, delayPosition = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    std::vector<FilterState> filterStates;
    double filterA = 0.0, filterB = 0.0;

    juce::Random random { (juce::int64) std::random_device {}() };   // a different seed in every instance
    double phase = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SonolintFixture)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SonolintFixture(); }
