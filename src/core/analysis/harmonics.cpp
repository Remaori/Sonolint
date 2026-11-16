#include "core/analysis/harmonics.h"

#include "core/analysis/db.h"
#include "core/analysis/spectrum.h"
#include "core/error.h"

namespace sonolint
{
HarmonicsResult measureHarmonics (std::span<const float> window, double sampleRate, double f0Hz)
{
    const auto power    = powerSpectrum (window);
    const auto binWidth = sampleRate / (double) (2 * (power.size() - 1));
    const auto f0Bin    = f0Hz / binWidth;
    const auto bandTop  = std::min (20000.0, 0.5 * sampleRate);

    // Below this the bins of the fundamental and of its harmonics would overlap.
    if (f0Bin <= 2 * kBinHalfWidth)
        throw Error (ErrorCode::config, "the sine is too low for its analysis window; make the signal longer");

    const auto fundamental = componentPower (power, f0Bin);

    HarmonicsResult result;
    result.f0Hz = f0Hz;
    double harmonics = 0.0;

    for (int k = 2; k <= 10; ++k)
    {
        // A harmonic counts while its whole component stays below 20 kHz and below Nyquist.
        if (k * f0Hz >= std::min (20000.0, 0.5 * sampleRate - kBinHalfWidth * binWidth))
            continue;

        const auto harmonic = componentPower (power, k * f0Bin);
        harmonics += harmonic;
        result.hDbc[(size_t) (k - 2)] = powerRatioToDb (harmonic, fundamental);
    }

    // Everything in the band, minus the bins of the fundamental.
    const auto firstBin = (size_t) std::ceil (20.0 / binWidth);
    const auto lastBin  = std::min ((size_t) std::floor (bandTop / binWidth), power.size() - 1);
    double residual = 0.0;

    for (auto bin = firstBin; bin <= lastBin; ++bin)
        if (std::abs ((double) bin - std::round (f0Bin)) > kBinHalfWidth)
            residual += power[bin];

    result.thdDb  = powerRatioToDb (harmonics, fundamental);
    result.thdnDb = powerRatioToDb (residual, fundamental);
    return result;
}
} // namespace sonolint
