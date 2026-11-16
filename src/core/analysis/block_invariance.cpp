#include "core/analysis/block_invariance.h"

#include "core/analysis/db.h"

namespace sonolint
{
BlockInvarianceResult measureBlockInvariance (std::span<const float> constantBlocks, std::span<const float> randomBlocks)
{
    const auto common = std::min (constantBlocks.size(), randomBlocks.size());
    double largest = 0.0, sum = 0.0;

    for (size_t n = 0; n < common; ++n)
    {
        const auto difference = (double) constantBlocks[n] - (double) randomBlocks[n];
        largest = std::max (largest, std::abs (difference));
        sum += difference * difference;
    }

    return { amplitudeToDb (largest), amplitudeToDb (common == 0 ? 0.0 : std::sqrt (sum / (double) common)) };
}
} // namespace sonolint
