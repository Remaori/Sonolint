#include "core/analysis/aliasing.h"
#include "core/analysis/spectrum.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr size_t kWindow = 91200;
} // namespace

TEST_CASE ("aliasing: a component at -80 dBc where the third harmonic folds back measures -80 dBc")
{
    const auto f0 = sonolint::snapToBin (9973.0, kSampleRate, 65536);
    const auto folded = kSampleRate - 3.0 * f0;   // 3 * f0 is 29.9 kHz, which comes back at 18.1 kHz

    const auto signal = test::tones (kSampleRate, { { f0, 0.5 }, { folded, 0.5e-4 } }, kWindow);
    const auto aliasing = sonolint::measureAliasing (signal, kSampleRate, f0);

    CHECK_THAT (aliasing.aliasingDbc, WithinAbs (-80.0, 0.1));

    // Harmonics 3 to 25 are above Nyquist; most of them fold to a place of their own.
    CHECK (aliasing.components + aliasing.skipped == 23);
    CHECK (aliasing.components > aliasing.skipped);
}

TEST_CASE ("aliasing: a clean sine is at the floor")
{
    const auto f0 = sonolint::snapToBin (9973.0, kSampleRate, 65536);
    const auto aliasing = sonolint::measureAliasing (test::tones (kSampleRate, { { f0, 0.5 } }, kWindow), kSampleRate, f0);

    CHECK (aliasing.aliasingDbc < -120.0);
}

TEST_CASE ("aliasing: at 12 kHz and 48 kHz every harmonic folds onto the fundamental, DC or Nyquist")
{
    const auto aliasing = sonolint::measureAliasing (test::tones (kSampleRate, { { 12000.0, 0.5 } }, kWindow), kSampleRate, 12000.0);

    CHECK (aliasing.components == 0);
    CHECK (aliasing.skipped == 23);
}
