#pragma once

#include <span>

namespace sonolint
{
struct BlockInvarianceResult
{
    double maxDiffDbfs = 0.0;   // the largest difference between the two renders
    double rmsDiffDbfs = 0.0;   // the RMS of the difference
};

// The same channel rendered twice: once in blocks of block_size and once in blocks
// of random sizes from 1 to block_size. A plugin that handles blocks correctly gives
// the same audio however the host cuts it.
BlockInvarianceResult measureBlockInvariance (std::span<const float> constantBlocks, std::span<const float> randomBlocks);
} // namespace sonolint
