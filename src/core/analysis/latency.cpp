#include "core/analysis/latency.h"

#include "core/analysis/fft.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace sonolint
{
LatencyResult measureLatency (std::span<const float> input, std::span<const float> output, int reportedSamples)
{
    // Cross-correlation through the FFT: correlation[lag] is the sum of
    // output[n + lag] * input[n]. Padding to more than both lengths keeps the lags apart.
    const auto size = std::bit_ceil (input.size() + output.size());
    std::vector<double> in (size, 0.0), out (size, 0.0);
    std::copy (input.begin(), input.end(), in.begin());
    std::copy (output.begin(), output.end(), out.begin());

    auto spectrum = realFft (out);
    const auto inputSpectrum = realFft (in);

    for (size_t k = 0; k < spectrum.size(); ++k)
        spectrum[k] *= std::conj (inputSpectrum[k]);

    const auto correlation = inverseRealFft (spectrum, size);
    const auto strength = [&] (size_t lag) { return std::abs (correlation[lag % size]); };

    size_t best = 0;

    for (size_t lag = 1; lag < size; ++lag)
        if (strength (lag) > strength (best))
            best = lag;

    // A parabola through the peak and its two neighbours finds the delay between samples.
    // shortcut: a silent output measures as 0 samples, report it as unmeasurable once scenarios have statuses
    const auto before = strength (best + size - 1), peak = strength (best), after = strength (best + 1);
    const auto curvature = before - 2.0 * peak + after;
    const auto fraction = curvature < 0.0 ? 0.5 * (before - after) / curvature : 0.0;

    // The second half of a circular correlation holds the negative lags.
    const auto lag = best < size / 2 ? (double) best : (double) best - (double) size;

    LatencyResult result;
    result.reportedSamples = reportedSamples;
    result.measuredSamples = lag + fraction;
    result.mismatchSamples = result.measuredSamples - reportedSamples;
    return result;
}
} // namespace sonolint
