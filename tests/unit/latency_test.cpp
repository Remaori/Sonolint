#include "core/analysis/latency.h"
#include "core/signals/generators.h"
#include "fake_renderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kTail = 4800;   // 100 ms of silence after the input, as in a real run

sonolint::LatencyResult latencyOf (const sonolint::Signal& signal, test::FakeRenderer::Process process, int reported)
{
    const auto rendered = test::FakeRenderer (std::move (process), reported).render (test::requestFor (signal, kTail));
    return sonolint::measureLatency (signal.audio.channels[0], rendered.output.channels[0], rendered.reportedLatencySamples);
}
} // namespace

TEST_CASE ("latency: a delay of 37 samples measures 37.00, with an impulse and with noise")
{
    for (const auto& signal : { sonolint::makeImpulse (kSampleRate), sonolint::makeWhiteNoise (kSampleRate, -18.0, 1.0),
                                sonolint::makePinkNoise (kSampleRate, -18.0, 1.0) })
    {
        const auto honest = latencyOf (signal, test::delay (37), 37);
        CHECK (honest.reportedSamples == 37);
        CHECK_THAT (honest.measuredSamples, WithinAbs (37.0, 0.05));
        CHECK_THAT (honest.mismatchSamples, WithinAbs (0.0, 0.05));

        // The same plugin telling the host that it has no latency.
        const auto lying = latencyOf (signal, test::delay (37), 0);
        CHECK_THAT (lying.measuredSamples, WithinAbs (37.0, 0.05));
        CHECK_THAT (lying.mismatchSamples, WithinAbs (37.0, 0.05));
    }
}

TEST_CASE ("latency: a delay of 10.5 samples measures 10.5")
{
    for (const auto& signal : { sonolint::makeImpulse (kSampleRate), sonolint::makeWhiteNoise (kSampleRate, -18.0, 1.0),
                                sonolint::makePinkNoise (kSampleRate, -18.0, 1.0) })
        CHECK_THAT (latencyOf (signal, test::delayPlusHalf (10), 10).measuredSamples, WithinAbs (10.5, 0.1));
}

TEST_CASE ("latency: no delay measures 0, and an inverted polarity doesn't matter")
{
    const auto signal = sonolint::makeWhiteNoise (kSampleRate, -18.0, 1.0);

    CHECK_THAT (latencyOf (signal, test::delay (0), 0).measuredSamples, WithinAbs (0.0, 0.05));

    const auto inverted = [] (const std::vector<float>& input)
    {
        auto output = test::delay (5) (input);

        for (auto& sample : output)
            sample = -sample;

        return output;
    };

    CHECK_THAT (latencyOf (signal, inverted, 5).measuredSamples, WithinAbs (5.0, 0.05));
}
