#include "core/analysis/level.h"

#include "core/analysis/db.h"

namespace sonolint
{
namespace
{
double rms (std::span<const float> samples)
{
    double sum = 0.0;

    for (const auto sample : samples)
        sum += (double) sample * sample;

    return samples.empty() ? 0.0 : std::sqrt (sum / (double) samples.size());
}
} // namespace

LevelResult measureLevel (std::span<const float> input, std::span<const float> output)
{
    double peak = 0.0, sum = 0.0;

    for (const auto sample : output)
    {
        peak = std::max (peak, std::abs ((double) sample));
        sum += sample;
    }

    LevelResult result;
    result.peakDbfs = amplitudeToDb (peak);
    result.rmsDbfs  = amplitudeToDb (rms (output));
    result.gainDb   = result.rmsDbfs - amplitudeToDb (rms (input));
    result.dc       = output.empty() ? 0.0 : sum / (double) output.size();
    return result;
}
} // namespace sonolint
