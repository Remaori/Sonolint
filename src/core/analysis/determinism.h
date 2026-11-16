#pragma once

#include <span>

namespace sonolint
{
struct DeterminismResult
{
    bool bitIdentical = true;
    double maxDiffDbfs = 0.0;   // the largest difference between the two renders
};

// `first` and `second` are the same channel from two renders of the same request,
// each with a fresh plugin instance. A difference means uninitialised memory or
// random state inside the plugin.
DeterminismResult measureDeterminism (std::span<const float> first, std::span<const float> second);
} // namespace sonolint
