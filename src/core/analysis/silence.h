#pragma once

#include <span>

namespace sonolint
{
struct SilenceResult
{
    double noiseFloorDbfs = 0.0;   // RMS of the output
    double peakDbfs = 0.0;
};

// `output` is the analysis window of one channel when the plugin was fed silence.
// Anything in it was added by the plugin: noise, hum or self-oscillation.
SilenceResult measureSilence (std::span<const float> output);
} // namespace sonolint
