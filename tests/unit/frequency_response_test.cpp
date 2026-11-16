#include "core/analysis/frequency_response.h"
#include "core/signals/generators.h"
#include "fake_renderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <complex>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr size_t kSkip = 4800;   // 100 ms

sonolint::FrequencyResponseResult responseOf (test::FakeRenderer::Process process)
{
    const auto freqs = sonolint::thirdOctaveFrequencies();
    const auto signal = sonolint::makeSteppedSine (kSampleRate, freqs, -24.0, 100.0);
    const auto rendered = test::FakeRenderer (std::move (process)).render (test::requestFor (signal));

    return sonolint::measureFrequencyResponse (signal.audio.channels[0], rendered.output.channels[0], signal.segments, kSampleRate, kSkip);
}
} // namespace

TEST_CASE ("frequency response: a first-order low-pass at 1 kHz matches its formula")
{
    const auto response = responseOf (test::lowPass (1000.0, kSampleRate));
    const test::OnePole filter (1000.0, kSampleRate);

    REQUIRE (response.freqsHz == sonolint::thirdOctaveFrequencies());
    REQUIRE (response.magnitudeDb.size() == 31);
    REQUIRE (response.phaseDeg.size() == 31);

    for (size_t i = 0; i < response.freqsHz.size(); ++i)
    {
        // H (z) = b (1 + z^-1) / (1 + a z^-1) on the unit circle.
        const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * response.freqsHz[i] / kSampleRate);
        const auto expected = filter.b * (1.0 + z1) / (1.0 + filter.a * z1);

        CAPTURE (response.freqsHz[i]);
        CHECK_THAT (response.magnitudeDb[i], WithinAbs (20.0 * std::log10 (std::abs (expected)), 0.02));
        CHECK_THAT (response.phaseDeg[i], WithinAbs (std::arg (expected) * 180.0 / std::numbers::pi, 0.5));
    }

    CHECK_THAT (response.gain1kDb, WithinAbs (-3.0103, 0.02));
}

TEST_CASE ("frequency response: a plugin that changes nothing is flat at 0 dB and 0 degrees")
{
    const auto response = responseOf ([] (const std::vector<float>& input) { return input; });

    for (size_t i = 0; i < response.freqsHz.size(); ++i)
    {
        CHECK_THAT (response.magnitudeDb[i], WithinAbs (0.0, 1.0e-9));
        CHECK_THAT (response.phaseDeg[i], WithinAbs (0.0, 1.0e-9));
    }

    CHECK_THAT (response.gain1kDb, WithinAbs (0.0, 1.0e-9));
}

TEST_CASE ("frequency response: the phase keeps going past -180 degrees instead of wrapping")
{
    // Two samples of delay: -360 * f * 2 / fs degrees, which is -300 at 20 kHz.
    const auto response = responseOf (test::delay (2));

    for (size_t i = 0; i < response.freqsHz.size(); ++i)
    {
        CAPTURE (response.freqsHz[i]);
        CHECK_THAT (response.magnitudeDb[i], WithinAbs (0.0, 0.02));
        CHECK_THAT (response.phaseDeg[i], WithinAbs (-360.0 * response.freqsHz[i] * 2.0 / kSampleRate, 0.5));
    }
}
