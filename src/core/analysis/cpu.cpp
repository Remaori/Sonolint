#include "core/analysis/cpu.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sonolint
{
CpuResult measureCpu (const std::vector<std::vector<double>>& blockSeconds, double audioSeconds)
{
    if (blockSeconds.empty() || audioSeconds <= 0.0)
        return {};

    const auto total = [] (const std::vector<double>& blocks) { return std::accumulate (blocks.begin(), blocks.end(), 0.0); };

    // Renders in order of total time; the one in the middle is the median.
    std::vector<size_t> order (blockSeconds.size());
    std::iota (order.begin(), order.end(), size_t (0));
    std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return total (blockSeconds[a]) < total (blockSeconds[b]); });

    auto blocks = blockSeconds[order[order.size() / 2]];
    std::sort (blocks.begin(), blocks.end());

    CpuResult result;
    result.realtimeFactor = total (blocks) / audioSeconds;
    result.corePercent    = 100.0 * result.realtimeFactor;

    if (! blocks.empty())
        result.p95BlockUs = 1.0e6 * blocks[(size_t) std::ceil (0.95 * (double) blocks.size()) - 1];

    return result;
}
} // namespace sonolint
