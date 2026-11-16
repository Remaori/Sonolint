#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace sonolint
{
struct TruePeakResult
{
    double truePeakDbtp = 0.0;     // peak of the wave rebuilt between the samples
    double samplePeakDbfs = 0.0;   // peak of the samples themselves
};

// Peaks of the samples [start, start + length) of a channel. The samples around the
// window are read too, so cutting the window out of a longer signal adds no edge.
TruePeakResult measureTruePeak (std::span<const float> channel, size_t start, size_t length, double sampleRate);

// How many points per sample the wave is rebuilt at, to reach about 192 kHz:
// 4 at 44.1 and 48 kHz, 2 at 88.2 and 96 kHz, 1 (nothing to do) from 176.4 kHz.
int truePeakOversampling (double sampleRate);

// The band-limited wave at `factor` points per sample, for the samples
// [start, start + length): length * factor values. Beyond the ends of the channel
// the signal counts as silence.
std::vector<double> oversample (std::span<const float> channel, size_t start, size_t length, int factor);
} // namespace sonolint
