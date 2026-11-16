#include "core/analysis/aliasing.h"

#include "core/analysis/db.h"
#include "core/analysis/spectrum.h"

#include <vector>

namespace sonolint
{
AliasingResult measureAliasing (std::span<const float> window, double sampleRate, double f0Hz)
{
    const auto power    = powerSpectrum (window);
    const auto binWidth = sampleRate / (double) (2 * (power.size() - 1));
    const auto nyquist  = 0.5 * sampleRate;

    // Where the signal has content of its own: DC, Nyquist, the fundamental and the
    // harmonics that stay below Nyquist. A folded harmonic is only measured when its
    // bins (W on each side) can't overlap the bins of one of these.
    std::vector<double> occupied { 0.0, nyquist };

    for (int k = 1; k * f0Hz <= nyquist; ++k)
        occupied.push_back (k * f0Hz);

    std::vector<bool> counted (power.size(), false);   // a bin is added once even if two harmonics fold onto it
    AliasingResult result;

    for (int k = 2; k <= 25; ++k)
    {
        if (k * f0Hz <= nyquist)
            continue;

        auto folded = std::fmod (k * f0Hz, sampleRate);

        if (folded > nyquist)
            folded = sampleRate - folded;

        const auto clear = std::none_of (occupied.begin(), occupied.end(), [&] (double freq)
        {
            return std::abs (folded - freq) <= 2 * kBinHalfWidth * binWidth;
        });

        if (! clear)
        {
            ++result.skipped;
            continue;
        }

        ++result.components;
        const auto centre = std::llround (folded / binWidth);

        for (auto bin = centre - kBinHalfWidth; bin <= centre + kBinHalfWidth; ++bin)
            counted[(size_t) bin] = true;   // inside the spectrum: `clear` keeps it 2 W away from both ends
    }

    double folded = 0.0;

    for (size_t bin = 0; bin < power.size(); ++bin)
        if (counted[bin])
            folded += power[bin];

    result.aliasingDbc = powerRatioToDb (folded, componentPower (power, f0Hz / binWidth));
    return result;
}
} // namespace sonolint
