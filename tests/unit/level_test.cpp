#include "core/analysis/level.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
// 1 kHz at 48 kHz: 48 samples per cycle, so one second holds exactly 1000 cycles.
const auto kSine = test::tones (48000.0, { { 1000.0, 0.5 } }, 48000);
} // namespace

TEST_CASE ("level: a sine of amplitude 0.5 peaks at -6.021 dBFS and has an RMS of -9.031 dBFS")
{
    const auto level = sonolint::measureLevel (kSine, kSine);

    CHECK_THAT (level.peakDbfs, WithinAbs (-6.021, 0.001));
    CHECK_THAT (level.rmsDbfs, WithinAbs (-9.031, 0.001));
    CHECK_THAT (level.gainDb, WithinAbs (0.0, 1.0e-9));
    CHECK_THAT (level.dc, WithinAbs (0.0, 1.0e-9));
}

TEST_CASE ("level: gain_db is the RMS of the output over the RMS of the input")
{
    auto halved = kSine;

    for (auto& sample : halved)
        sample *= 0.5f;

    CHECK_THAT (sonolint::measureLevel (kSine, halved).gainDb, WithinAbs (-6.0206, 0.001));
}

TEST_CASE ("level: dc is the mean of the output")
{
    auto shifted = kSine;

    for (auto& sample : shifted)
        sample += 0.25f;

    CHECK_THAT (sonolint::measureLevel (kSine, shifted).dc, WithinAbs (0.25, 1.0e-6));
}

TEST_CASE ("level: silence gives finite numbers")
{
    const std::vector<float> silence (48000, 0.0f);
    const auto level = sonolint::measureLevel (silence, silence);

    CHECK_THAT (level.peakDbfs, WithinAbs (-300.0, 1.0e-9));
    CHECK_THAT (level.rmsDbfs, WithinAbs (-300.0, 1.0e-9));
    CHECK_THAT (level.gainDb, WithinAbs (0.0, 1.0e-9));
}
