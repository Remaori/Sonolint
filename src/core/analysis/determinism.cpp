#include "core/analysis/determinism.h"

#include "core/analysis/db.h"

#include <cstring>

namespace sonolint
{
DeterminismResult measureDeterminism (std::span<const float> first, std::span<const float> second)
{
    const auto common = std::min (first.size(), second.size());
    double largest = 0.0;

    for (size_t n = 0; n < common; ++n)
        largest = std::max (largest, std::abs ((double) first[n] - (double) second[n]));

    DeterminismResult result;
    result.bitIdentical = first.size() == second.size() && std::memcmp (first.data(), second.data(), first.size_bytes()) == 0;
    result.maxDiffDbfs  = amplitudeToDb (largest);
    return result;
}
} // namespace sonolint
