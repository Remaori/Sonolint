#include "core/analysis/true_peak.h"

#include "core/analysis/db.h"

#include <numbers>

namespace sonolint
{
namespace
{
// A sinc under a Kaiser window. The reference filter of ITU-R BS.1770 has 12 taps per
// phase; 64 keep the ripple far below 0.1 dB up to 0.45 times the sample rate.
constexpr int    kTapsPerPhase = 64;
constexpr double kKaiserBeta   = 8.0;

// Modified Bessel function of the first kind and order 0, which the Kaiser window needs.
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;

    for (int k = 1; term > 1.0e-17 * sum; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }

    return sum;
}

// Weight of a sample that is `distance` samples away from the point being rebuilt.
// Never called with 0: the points that fall on a sample are the samples themselves.
double kernel (double distance)
{
    const auto half = kTapsPerPhase / 2.0;

    if (std::abs (distance) >= half)
        return 0.0;

    const auto x = std::numbers::pi * distance;
    const auto window = besselI0 (kKaiserBeta * std::sqrt (1.0 - (distance / half) * (distance / half))) / besselI0 (kKaiserBeta);
    return std::sin (x) / x * window;
}
} // namespace

int truePeakOversampling (double sampleRate)
{
    return sampleRate < 80000.0 ? 4 : sampleRate < 160000.0 ? 2 : 1;
}

std::vector<double> oversample (std::span<const float> channel, size_t start, size_t length, int factor)
{
    // weights[phase][tap]: tap j reads the sample (j - half + 1) places from the current one.
    constexpr int half = kTapsPerPhase / 2;
    std::vector<std::vector<double>> weights ((size_t) factor, std::vector<double> (kTapsPerPhase));

    for (int phase = 1; phase < factor; ++phase)
        for (int tap = 0; tap < kTapsPerPhase; ++tap)
            weights[(size_t) phase][(size_t) tap] = kernel ((double) (tap - half + 1) - (double) phase / factor);

    std::vector<double> result;
    result.reserve (length * (size_t) factor);

    for (auto n = start; n < start + length; ++n)
    {
        result.push_back (channel[n]);

        for (int phase = 1; phase < factor; ++phase)
        {
            double sum = 0.0;

            for (int tap = 0; tap < kTapsPerPhase; ++tap)
            {
                const auto index = (long long) n + tap - half + 1;

                if (index >= 0 && index < (long long) channel.size())
                    sum += (double) channel[(size_t) index] * weights[(size_t) phase][(size_t) tap];
            }

            result.push_back (sum);
        }
    }

    return result;
}

TruePeakResult measureTruePeak (std::span<const float> channel, size_t start, size_t length, double sampleRate)
{
    double samplePeak = 0.0;

    for (const auto sample : channel.subspan (start, length))
        samplePeak = std::max (samplePeak, std::abs ((double) sample));

    auto truePeak = samplePeak;
    const auto factor = truePeakOversampling (sampleRate);

    if (factor > 1)
        for (const auto value : oversample (channel, start, length, factor))
            truePeak = std::max (truePeak, std::abs (value));

    return { amplitudeToDb (truePeak), amplitudeToDb (samplePeak) };
}
} // namespace sonolint
