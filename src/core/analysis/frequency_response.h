#pragma once

#include "core/signals/signal.h"

#include <cstddef>
#include <span>
#include <vector>

namespace sonolint
{
struct FrequencyResponseResult
{
    std::vector<double> freqsHz;
    std::vector<double> magnitudeDb;
    std::vector<double> phaseDeg;   // unwrapped from one frequency to the next
    double gain1kDb = 0.0;          // the magnitude at the point closest to 1 kHz
};

// `input` is one channel of a stepped sine and `segments` its steps; `output` is the
// same channel after the plugin, already aligned with the input. Each step is
// measured without its first skipSamples.
// Throws Error (config) if a step is too short to hold one cycle after the skip.
FrequencyResponseResult measureFrequencyResponse (std::span<const float> input, std::span<const float> output,
                                                  std::span<const Segment> segments, double sampleRate, size_t skipSamples);
} // namespace sonolint
