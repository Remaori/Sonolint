#include "core/analysis/harmonics.h"
#include "core/analysis/spectrum.h"
#include "core/signals/prng.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr size_t kWindow = 91200;   // 2 s minus the 100 ms that are skipped: analysed with a 65536-point FFT
} // namespace

TEST_CASE ("harmonics: H2 at -40 dBc and H3 at -60 dBc give a THD of -39.96 dB")
{
    const auto f0 = sonolint::snapToBin (1000.0, kSampleRate, 65536);
    const auto signal = test::tones (kSampleRate, { { f0, 1.0 }, { 2.0 * f0, 0.01 }, { 3.0 * f0, 0.001 } }, kWindow);
    const auto harmonics = sonolint::measureHarmonics (signal, kSampleRate, f0);

    CHECK (harmonics.f0Hz == f0);
    REQUIRE (harmonics.hDbc[0].has_value());
    REQUIRE (harmonics.hDbc[1].has_value());
    CHECK_THAT (*harmonics.hDbc[0], WithinAbs (-40.0, 0.05));
    CHECK_THAT (*harmonics.hDbc[1], WithinAbs (-60.0, 0.05));
    CHECK_THAT (harmonics.thdDb, WithinAbs (-39.96, 0.05));

    // Nothing else in the signal: H4 to H10 are at the floor and THD+N is the THD.
    for (size_t i = 2; i < harmonics.hDbc.size(); ++i)
    {
        REQUIRE (harmonics.hDbc[i].has_value());
        CHECK (*harmonics.hDbc[i] < -120.0);
    }

    CHECK_THAT (harmonics.thdnDb, WithinAbs (-39.96, 0.05));
}

TEST_CASE ("harmonics: THD+N of a sine in white noise is the noise between 20 Hz and 20 kHz over the sine")
{
    constexpr double amplitude = 0.5, noiseRms = 0.001;
    const auto f0 = sonolint::snapToBin (1000.0, kSampleRate, 65536);

    sonolint::Prng prng (1);
    std::vector<float> signal (kWindow);

    for (size_t n = 0; n < kWindow; ++n)
        signal[n] = (float) (amplitude * std::sin (2.0 * std::numbers::pi * f0 * (double) n / kSampleRate) + noiseRms * prng.gaussian());

    // White noise spreads evenly from 0 to 24 kHz, and 19.98 kHz of that is in the band.
    const auto noiseInBand = noiseRms * noiseRms * (20000.0 - 20.0) / (kSampleRate / 2.0);
    const auto expected = 10.0 * std::log10 (noiseInBand / (amplitude * amplitude / 2.0));

    CHECK_THAT (sonolint::measureHarmonics (signal, kSampleRate, f0).thdnDb, WithinAbs (expected, 0.3));
}

TEST_CASE ("harmonics: a harmonic above 20 kHz has no value")
{
    const auto f0 = sonolint::snapToBin (9973.0, kSampleRate, 65536);
    const auto harmonics = sonolint::measureHarmonics (test::tones (kSampleRate, { { f0, 0.5 } }, kWindow), kSampleRate, f0);

    CHECK (harmonics.hDbc[0].has_value());   // 19.9 kHz
    CHECK_FALSE (harmonics.hDbc[1].has_value());   // 29.9 kHz
    CHECK_FALSE (harmonics.hDbc[8].has_value());
}
