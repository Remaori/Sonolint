#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace sonolint
{
// FFT of real data of any length. The result has x.size() / 2 + 1 bins: bin k is the
// frequency k * sampleRate / x.size(). Not scaled.
std::vector<std::complex<double>> realFft (std::span<const double> x);

// The inverse: `size` real samples from size / 2 + 1 bins, scaled so that
// inverseRealFft (realFft (x), x.size()) gives x back.
std::vector<double> inverseRealFft (std::span<const std::complex<double>> spectrum, size_t size);
} // namespace sonolint
