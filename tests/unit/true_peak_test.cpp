#include "core/analysis/true_peak.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = std::numbers::pi;
} // namespace

TEST_CASE ("true peak: a sine at fs/4 with a 45 degree phase hides 3 dB between its samples")
{
    // Every sample lands at 0.707 of the peak: the classic inter-sample peak.
    const auto sine = test::tones (kSampleRate, { { kSampleRate / 4.0, 1.0, kPi / 4.0 } }, 48000);
    const auto peaks = sonolint::measureTruePeak (sine, 4800, 38400, kSampleRate);

    CHECK_THAT (peaks.samplePeakDbfs, WithinAbs (-3.01, 0.01));
    CHECK_THAT (peaks.truePeakDbtp, WithinAbs (0.0, 0.2));
}

TEST_CASE ("true peak: the interpolation stays within 0.05 dB of the real wave up to 0.45 fs")
{
    for (const auto relativeFreq : { 0.01, 0.1, 0.25, 0.4, 0.45 })
    {
        const auto freq = relativeFreq * kSampleRate;
        const auto sine = test::tones (kSampleRate, { { freq, 0.5, 0.3 } }, 4000);
        const auto oversampled = sonolint::oversample (sine, 1000, 2000, 4);

        REQUIRE (oversampled.size() == 8000);
        double worst = 0.0;

        for (size_t m = 0; m < oversampled.size(); ++m)
        {
            const auto time = (1000.0 + (double) m / 4.0) / kSampleRate;
            worst = std::max (worst, std::abs (oversampled[m] - 0.5 * std::sin (2.0 * kPi * freq * time + 0.3)));
        }

        CAPTURE (relativeFreq);
        CHECK (worst < 0.5 * 0.0058);   // 0.05 dB of the amplitude
    }
}

TEST_CASE ("true peak: 4x oversampling at 44.1 and 48 kHz, 2x at 88.2 and 96 kHz, none above")
{
    CHECK (sonolint::truePeakOversampling (44100.0) == 4);
    CHECK (sonolint::truePeakOversampling (48000.0) == 4);
    CHECK (sonolint::truePeakOversampling (88200.0) == 2);
    CHECK (sonolint::truePeakOversampling (96000.0) == 2);
    CHECK (sonolint::truePeakOversampling (176400.0) == 1);
    CHECK (sonolint::truePeakOversampling (192000.0) == 1);
}

TEST_CASE ("true peak: never below the sample peak, and equal to it for a signal with nothing in between")
{
    const std::vector<float> flat (2000, 0.25f);
    const auto peaks = sonolint::measureTruePeak (flat, 500, 1000, kSampleRate);

    CHECK_THAT (peaks.samplePeakDbfs, WithinAbs (-12.0412, 0.001));
    CHECK_THAT (peaks.truePeakDbtp, WithinAbs (-12.0412, 0.001));
    CHECK (peaks.truePeakDbtp >= peaks.samplePeakDbfs);
}
