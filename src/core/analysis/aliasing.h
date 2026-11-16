#pragma once

#include <span>

namespace sonolint
{
struct AliasingResult
{
    double aliasingDbc = 0.0;   // the folded harmonics over the fundamental
    int components = 0;         // folded harmonics that were measured
    int skipped = 0;            // folded harmonics left out because they land on something else
};

// `window` is the analysis window of one channel that carries a sine at f0Hz.
// When more than half of the harmonics are skipped, the frequency is a bad choice:
// pick one that doesn't divide the sample rate, like 9973 Hz.
// Throws Error (config) if the window is too short for the FFT.
AliasingResult measureAliasing (std::span<const float> window, double sampleRate, double f0Hz);
} // namespace sonolint
