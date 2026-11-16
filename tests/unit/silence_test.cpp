#include "core/analysis/silence.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE ("silence: an output of zeros gives finite floors")
{
    const std::vector<float> output (48000, 0.0f);
    const auto silence = sonolint::measureSilence (output);

    CHECK_THAT (silence.noiseFloorDbfs, WithinAbs (-300.0, 1.0e-9));
    CHECK_THAT (silence.peakDbfs, WithinAbs (-300.0, 1.0e-9));
}

TEST_CASE ("silence: noise added by the plugin shows up as its RMS and its peak")
{
    // 0.001 with alternating sign, and one sample ten times bigger.
    std::vector<float> output (10000);

    for (size_t n = 0; n < output.size(); ++n)
        output[n] = n % 2 == 0 ? 0.001f : -0.001f;

    CHECK_THAT (sonolint::measureSilence (output).noiseFloorDbfs, WithinAbs (-60.0, 0.001));
    CHECK_THAT (sonolint::measureSilence (output).peakDbfs, WithinAbs (-60.0, 0.001));

    output[5000] = 0.01f;
    CHECK_THAT (sonolint::measureSilence (output).peakDbfs, WithinAbs (-40.0, 0.001));
}
