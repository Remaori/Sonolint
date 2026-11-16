#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace sonolint
{
// W: the power of a spectral component is the sum of the bins within W of its centre.
constexpr int kBinHalfWidth = 6;

// The FFT size used to analyse a window of this many samples: the largest power of
// two that fits, between 2^14 and 2^18. Throws Error (config) if the window is shorter.
size_t analysisFftSize (size_t windowSamples);

// The frequency of the FFT bin closest to freqHz (never bin 0).
double snapToBin (double freqHz, double sampleRate, size_t fftSize);

// |X[k]|^2 of the first analysisFftSize (window.size()) samples under a
// Blackman-Harris window: fftSize / 2 + 1 values. Only ratios between them mean anything.
std::vector<double> powerSpectrum (std::span<const float> window);

// Sum of the bins within W of `bin`, which doesn't have to be a whole number.
double componentPower (std::span<const double> power, double bin);
} // namespace sonolint
