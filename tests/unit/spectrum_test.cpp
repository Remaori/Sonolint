#include "core/analysis/db.h"
#include "core/analysis/fft.h"
#include "core/analysis/spectrum.h"
#include "core/analysis/window.h"
#include "core/error.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numeric>

using Catch::Matchers::WithinAbs;

TEST_CASE ("realFft puts a cosine in its bin, and inverseRealFft undoes it for any length")
{
    constexpr size_t size = 1000;   // not a power of two on purpose
    std::vector<double> x (size);

    for (size_t n = 0; n < size; ++n)
        x[n] = std::cos (2.0 * std::numbers::pi * 5.0 * (double) n / (double) size);

    const auto spectrum = sonolint::realFft (x);
    REQUIRE (spectrum.size() == size / 2 + 1);
    CHECK_THAT (spectrum[5].real(), WithinAbs (size / 2.0, 1.0e-9));
    CHECK_THAT (std::abs (spectrum[4]), WithinAbs (0.0, 1.0e-9));
    CHECK_THAT (std::abs (spectrum[6]), WithinAbs (0.0, 1.0e-9));

    const auto back = sonolint::inverseRealFft (spectrum, size);

    for (size_t n = 0; n < size; ++n)
        REQUIRE_THAT (back[n], WithinAbs (x[n], 1.0e-12));
}

TEST_CASE ("the analysis FFT is the largest power of two that fits, between 2^14 and 2^18")
{
    CHECK (sonolint::analysisFftSize (16384) == 16384);
    CHECK (sonolint::analysisFftSize (91200) == 65536);   // 2 s at 48 kHz minus 100 ms
    CHECK (sonolint::analysisFftSize (1 << 20) == (1 << 18));

    try
    {
        sonolint::analysisFftSize (16383);
        FAIL ("a window shorter than 2^14 samples must be rejected");
    }
    catch (const sonolint::Error& e)
    {
        CHECK (e.code == sonolint::ErrorCode::config);
    }
}

TEST_CASE ("snapToBin moves a frequency to the closest FFT bin")
{
    CHECK_THAT (sonolint::snapToBin (9973.0, 48000.0, 65536), WithinAbs (13616.0 * 48000.0 / 65536.0, 1.0e-9));
    CHECK_THAT (sonolint::snapToBin (0.1, 48000.0, 65536), WithinAbs (48000.0 / 65536.0, 1.0e-12));   // never bin 0
}

TEST_CASE ("the Blackman-Harris window is 1 in the middle and almost 0 at the start")
{
    const auto window = sonolint::blackmanHarris (1024);
    CHECK_THAT (window[512], WithinAbs (1.0, 1.0e-12));
    CHECK_THAT (window[0], WithinAbs (0.00006, 1.0e-12));
}

TEST_CASE ("a sine on a bin keeps all its power within W bins of it")
{
    constexpr double sampleRate = 48000.0;
    constexpr size_t size = 65536;
    const auto freq = sonolint::snapToBin (1000.0, sampleRate, size);
    const auto power = sonolint::powerSpectrum (test::tones (sampleRate, { { freq, 0.5 } }, size));

    REQUIRE (power.size() == size / 2 + 1);

    const auto bin   = freq * (double) size / sampleRate;
    const auto tone  = sonolint::componentPower (power, bin);
    const auto total = std::accumulate (power.begin(), power.end(), 0.0);

    CHECK (sonolint::powerToDb ((total - tone) / tone) < -120.0);
}
