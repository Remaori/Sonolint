#include "core/analysis/silence.h"

#include "core/analysis/db.h"

namespace sonolint
{
SilenceResult measureSilence (std::span<const float> output)
{
    double peak = 0.0, sum = 0.0;

    for (const auto sample : output)
    {
        peak = std::max (peak, std::abs ((double) sample));
        sum += (double) sample * sample;
    }

    return { amplitudeToDb (output.empty() ? 0.0 : std::sqrt (sum / (double) output.size())), amplitudeToDb (peak) };
}
} // namespace sonolint
