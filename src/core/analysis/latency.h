#pragma once

#include <span>

namespace sonolint
{
struct LatencyResult
{
    int reportedSamples = 0;        // what the plugin tells the host
    double measuredSamples = 0.0;   // where the output actually lines up with the input
    double mismatchSamples = 0.0;   // measured minus reported
};

// `input` is one channel of an impulse or of noise and `output` the same channel as
// the plugin returned it: raw, not yet aligned. A periodic signal such as a sine has
// no single answer and must not be used.
LatencyResult measureLatency (std::span<const float> input, std::span<const float> output, int reportedSamples);
} // namespace sonolint
