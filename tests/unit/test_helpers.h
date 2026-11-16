#pragma once

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <numbers>
#include <vector>

namespace test
{
struct Tone
{
    double freqHz, amplitude, phase = 0.0;   // phase in radians
};

// The sum of the tones, each one amplitude * sin (2 pi f n / fs + phase), added in
// double and converted to the float samples a plugin would get.
inline std::vector<float> tones (double sampleRate, std::initializer_list<Tone> list, size_t numSamples)
{
    std::vector<float> samples (numSamples);

    for (size_t n = 0; n < numSamples; ++n)
    {
        double sum = 0.0;

        for (const auto& tone : list)
            sum += tone.amplitude * std::sin (2.0 * std::numbers::pi * tone.freqHz * (double) n / sampleRate + tone.phase);

        samples[n] = (float) sum;
    }

    return samples;
}
} // namespace test
