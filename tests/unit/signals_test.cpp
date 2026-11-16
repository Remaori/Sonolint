#include "core/analysis/db.h"
#include "core/analysis/spectrum.h"
#include "core/error.h"
#include "core/io/wav.h"
#include "core/signals/generators.h"
#include "core/signals/prng.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <span>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSampleRate = 48000.0;

double rmsDb (std::span<const float> samples)
{
    double sum = 0.0;

    for (const auto sample : samples)
        sum += (double) sample * sample;

    return sonolint::amplitudeToDb (std::sqrt (sum / (double) samples.size()));
}

// The steady part of a signal: its first segment without the first 100 ms.
std::span<const float> steadyPart (const sonolint::Signal& signal)
{
    const auto skip = (size_t) (0.1 * signal.audio.sampleRate);
    return std::span (signal.audio.channels[0]).subspan (skip, signal.segments[0].length - skip);
}

// Slope of the power density in dB per octave, fitted over the octaves from 63 Hz to 8 kHz.
double spectrumSlope (const sonolint::Signal& signal)
{
    const auto window = steadyPart (signal);
    const auto power = sonolint::powerSpectrum (window);
    const auto binWidth = signal.audio.sampleRate / (double) sonolint::analysisFftSize (window.size());

    constexpr int numBands = 8;
    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;

    for (int band = 0; band < numBands; ++band)
    {
        const auto centre = 62.5 * std::pow (2.0, band);
        const auto first = (size_t) std::ceil (centre / std::numbers::sqrt2 / binWidth);
        const auto last  = (size_t) std::floor (centre * std::numbers::sqrt2 / binWidth);
        double sum = 0.0;

        for (auto k = first; k <= last; ++k)
            sum += power[k];

        const auto x = (double) band;
        const auto y = 10.0 * std::log10 (sum / (double) (last - first + 1));
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }

    return (numBands * sumXY - sumX * sumY) / (numBands * sumXX - sumX * sumX);
}
} // namespace

TEST_CASE ("the generator gives the golden xoshiro256** values for seed 1")
{
    // From an independent implementation of SplitMix64 seeding + xoshiro256**.
    sonolint::Prng prng (1);
    CHECK (prng.next() == 0xb3f2af6d0fc710c5ull);
    CHECK (prng.next() == 0x853b559647364ceaull);
    CHECK (prng.next() == 0x92f89756082a4514ull);
    CHECK (prng.next() == 0x642e1c7bc266a3a7ull);
    CHECK (prng.next() == 0xb27a48e29a233673ull);

    CHECK (sonolint::Prng (1).uniform() == 0.7029218331588506);   // exact: only integer arithmetic
}

TEST_CASE ("gaussian values have mean 0 and variance 1")
{
    sonolint::Prng prng (1);
    constexpr int count = 200000;
    double sum = 0.0, sumSquares = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const auto value = prng.gaussian();
        sum += value;
        sumSquares += value * value;
    }

    CHECK_THAT (sum / count, WithinAbs (0.0, 0.01));
    CHECK_THAT (sumSquares / count, WithinAbs (1.0, 0.01));
}

TEST_CASE ("sine: level_dbfs is the peak, with fades that stay out of its segment")
{
    const auto signal = sonolint::makeSine (kSampleRate, 1000.0, -6.0, 2.0);
    const auto& samples = signal.audio.channels[0];
    const auto amplitude = std::pow (10.0, -6.0 / 20.0);

    REQUIRE (samples.size() == 96000);
    REQUIRE (signal.segments.size() == 1);
    CHECK (signal.segments[0].start == 0);
    CHECK (signal.segments[0].length == 96000 - 240);   // without the 5 ms fade-out
    CHECK (signal.segments[0].freqHz == 1000.0);

    CHECK (samples.front() == 0.0f);
    CHECK_THAT (samples.back(), WithinAbs (0.0, 1.0e-4));

    // After the fade-in, exactly the sine.
    for (size_t n = 240; n < 96000 - 240; n += 97)
        REQUIRE_THAT (samples[n], WithinAbs (amplitude * std::sin (2.0 * std::numbers::pi * 1000.0 * (double) n / kSampleRate), 1.0e-7));

    // phase_deg 90 starts at the top of the wave.
    const auto shifted = sonolint::makeSine (kSampleRate, 1000.0, -6.0, 2.0, 90.0);
    CHECK_THAT (shifted.audio.channels[0][4800], WithinAbs (amplitude, 1.0e-7));
}

TEST_CASE ("stepped sine: one segment per frequency, long enough and without jumps")
{
    const auto freqs = sonolint::thirdOctaveFrequencies();
    REQUIRE (freqs.size() == 31);
    CHECK (sonolint::octaveFrequencies().size() == 10);

    const auto signal = sonolint::makeSteppedSine (kSampleRate, freqs, -24.0, 100.0);
    const auto& samples = signal.audio.channels[0];
    const auto amplitude = std::pow (10.0, -24.0 / 20.0);

    REQUIRE (signal.segments.size() == 31);
    CHECK (signal.segments.front().freqHz == 20.0);
    CHECK (signal.segments.front().length == 124800);   // 50 cycles of 20 Hz (2.5 s) + 100 ms
    CHECK (signal.segments[17].freqHz == 1000.0);
    CHECK (signal.segments[17].length == 16800);        // 0.25 s + 100 ms

    size_t expectedStart = 0;

    for (const auto& segment : signal.segments)
    {
        CHECK (segment.start == expectedStart);
        expectedStart += segment.length;
    }

    CHECK (samples.size() == expectedStart + 240);   // plus the fade-out after the last step

    // The wave continues across steps: no step is bigger than the steepest sine allows.
    for (size_t i = 1; i < signal.segments.size(); ++i)
    {
        const auto at = signal.segments[i].start;
        const auto steepest = amplitude * 2.0 * std::numbers::pi * signal.segments[i].freqHz / kSampleRate;
        CHECK (std::abs ((double) samples[at] - samples[at - 1]) <= steepest * 1.001);
    }

    // Each step holds the level.
    const auto& oneKilohertz = signal.segments[17];
    CHECK_THAT (rmsDb (std::span (samples).subspan (oneKilohertz.start + 4800, 12000)), WithinAbs (-24.0 - 3.0103, 0.001));

    // Points above 0.45 * fs aren't generated: at 44.1 kHz that drops 20 kHz.
    CHECK (sonolint::makeSteppedSine (44100.0, freqs, -24.0, 100.0).segments.size() == 30);
}

TEST_CASE ("impulse: one sample, where position_ms says")
{
    const auto signal = sonolint::makeImpulse (kSampleRate);
    const auto& samples = signal.audio.channels[0];

    REQUIRE (samples.size() == 48000);
    CHECK (samples[2400] == 0.5f);
    CHECK (std::count (samples.begin(), samples.end(), 0.0f) == 47999);
    CHECK (signal.segments[0].length == 48000);
}

TEST_CASE ("white noise: level_dbfs is the RMS, the seed decides the samples, the spectrum is flat")
{
    const auto signal = sonolint::makeWhiteNoise (kSampleRate, -18.0, 10.0, 1);

    CHECK (signal.audio.channels[0].size() == 480000);
    CHECK (signal.segments[0].length == 480000 - 240);
    CHECK_THAT (rmsDb (steadyPart (signal)), WithinAbs (-18.0, 0.05));
    CHECK_THAT (spectrumSlope (signal), WithinAbs (0.0, 0.5));

    CHECK (sonolint::makeWhiteNoise (kSampleRate, -18.0, 1.0, 1).audio.channels == sonolint::makeWhiteNoise (kSampleRate, -18.0, 1.0, 1).audio.channels);
    CHECK (sonolint::makeWhiteNoise (kSampleRate, -18.0, 1.0, 1).audio.channels != sonolint::makeWhiteNoise (kSampleRate, -18.0, 1.0, 2).audio.channels);
}

TEST_CASE ("pink noise falls 3 dB per octave")
{
    const auto signal = sonolint::makePinkNoise (kSampleRate, -18.0, 10.0, 1);

    CHECK_THAT (spectrumSlope (signal), WithinAbs (-3.0103, 0.5));
    CHECK_THAT (rmsDb (steadyPart (signal)), WithinAbs (-18.0, 0.1));
}

TEST_CASE ("silence is all zeros")
{
    const auto signal = sonolint::makeSilence (kSampleRate, 0.5);
    const auto& samples = signal.audio.channels[0];

    CHECK (samples.size() == 24000);
    CHECK (std::count (samples.begin(), samples.end(), 0.0f) == 24000);
}

TEST_CASE ("file: the samples of the WAV, and only at its own sample rate")
{
    sonolint::AudioBuffer buffer;
    buffer.sampleRate = 44100.0;
    buffer.channels = { { 0.5f, -0.25f, 0.125f } };

    const auto path = std::filesystem::temp_directory_path() / "sonolint_signals_test.wav";
    sonolint::writeWav (path, buffer);

    const auto signal = sonolint::makeFromFile (path, 44100.0);
    CHECK (signal.audio.channels == buffer.channels);
    CHECK (signal.segments[0].length == 3);

    try
    {
        sonolint::makeFromFile (path, 48000.0);
        FAIL ("a file at another sample rate must be rejected: Sonolint doesn't resample");
    }
    catch (const sonolint::Error& e)
    {
        CHECK (e.code == sonolint::ErrorCode::config);
    }

    std::filesystem::remove (path);
}
