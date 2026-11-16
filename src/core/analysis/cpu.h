#pragma once

#include <vector>

namespace sonolint
{
struct CpuResult
{
    double realtimeFactor = 0.0;   // processing time over the duration of the audio
    double corePercent = 0.0;      // the same, as a percentage of one core
    double p95BlockUs = 0.0;       // 95 % of the blocks took this many microseconds or less
};

// blockSeconds holds, for each render, how long each processBlock call took.
// audioSeconds is how much audio each render processed. The figures are those of the
// median render, so one slow run doesn't decide them.
CpuResult measureCpu (const std::vector<std::vector<double>>& blockSeconds, double audioSeconds);
} // namespace sonolint
