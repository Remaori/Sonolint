#pragma once

#include <span>

namespace sonolint
{
struct LevelResult
{
    double peakDbfs = 0.0;   // of the output
    double rmsDbfs = 0.0;    // of the output
    double gainDb = 0.0;     // RMS of the output minus RMS of the input
    double dc = 0.0;         // mean of the output, linear
};

// `input` and `output` are the same analysis window of one channel.
LevelResult measureLevel (std::span<const float> input, std::span<const float> output);
} // namespace sonolint
