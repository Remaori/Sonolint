#include "core/analysis/spectrum.h"

#include "core/analysis/fft.h"
#include "core/analysis/window.h"
#include "core/error.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

namespace sonolint
{
size_t analysisFftSize (size_t windowSamples)
{
    constexpr auto smallest = size_t (1) << 14;
    constexpr auto largest  = size_t (1) << 18;

    if (windowSamples < smallest)
        throw Error (ErrorCode::config, "the signal is too short for spectral analysis: " + std::to_string (windowSamples)
                                            + " samples after the skipped part, and it needs at least " + std::to_string (smallest));

    return std::min (std::bit_floor (windowSamples), largest);
}

double snapToBin (double freqHz, double sampleRate, size_t fftSize)
{
    const auto binWidth = sampleRate / (double) fftSize;
    return std::max (1.0, std::round (freqHz / binWidth)) * binWidth;
}

std::vector<double> powerSpectrum (std::span<const float> window)
{
    const auto size = analysisFftSize (window.size());
    auto samples = blackmanHarris (size);

    for (size_t n = 0; n < size; ++n)
        samples[n] *= (double) window[n];

    const auto spectrum = realFft (samples);
    std::vector<double> power (spectrum.size());
    std::transform (spectrum.begin(), spectrum.end(), power.begin(), [] (const auto& bin) { return std::norm (bin); });
    return power;
}

double componentPower (std::span<const double> power, double bin)
{
    const auto centre = std::llround (bin);
    const auto first  = std::max<long long> (0, centre - kBinHalfWidth);
    const auto last   = std::min<long long> ((long long) power.size() - 1, centre + kBinHalfWidth);
    double sum = 0.0;

    for (auto k = first; k <= last; ++k)
        sum += power[(size_t) k];

    return sum;
}
} // namespace sonolint
