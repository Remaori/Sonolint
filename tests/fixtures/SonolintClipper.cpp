// SonolintClipper: a hard clip at Threshold. A hard clip makes odd harmonics without
// end, so at 1x the ones above half the sample rate fold back as aliasing. With
// Oversample at 4x most of them are filtered out before coming back down: the
// aliasing Sonolint measures must drop clearly.
#include "fixture.h"

#include <juce_dsp/juce_dsp.h>

class Clipper : public Fixture
{
public:
    Clipper() : thresholdDb (addFloat ("Threshold", -24.0f, 0.0f, -6.0f, "dB")), oversample (addChoice ("Oversample", { "1x", "4x" })) {}

    void prepareToPlay (double, int largestBlock) override
    {
        oversampling.reset();

        if (oversample->getIndex() == 1)
        {
            // 2 stages of 2x. The last argument asks for a latency that is a whole number of samples.
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) getTotalNumInputChannels(), 2,
                                                                             juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            oversampling->initProcessing ((size_t) largestBlock);
        }

        setLatencySamples (oversampling != nullptr ? (int) std::lround (oversampling->getLatencyInSamples()) : 0);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto threshold = juce::Decibels::decibelsToGain (thresholdDb->get());
        juce::dsp::AudioBlock<float> block (buffer);

        const auto clip = [threshold] (juce::dsp::AudioBlock<float> audio)
        {
            for (size_t channel = 0; channel < audio.getNumChannels(); ++channel)
                juce::FloatVectorOperations::clip (audio.getChannelPointer (channel), audio.getChannelPointer (channel), -threshold, threshold,
                                                   (int) audio.getNumSamples());
        };

        if (oversampling == nullptr)
        {
            clip (block);
            return;
        }

        clip (oversampling->processSamplesUp (block));
        oversampling->processSamplesDown (block);
    }

private:
    juce::AudioParameterFloat* thresholdDb;
    juce::AudioParameterChoice* oversample;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Clipper(); }
