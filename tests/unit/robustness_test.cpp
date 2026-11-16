#include "core/analysis/block_invariance.h"
#include "core/analysis/cpu.h"
#include "core/analysis/determinism.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE ("determinism: two equal renders are bit-identical, and one different sample is not")
{
    const std::vector<float> first { 0.0f, 0.5f, -0.25f, 1.0f };
    auto second = first;

    const auto same = sonolint::measureDeterminism (first, second);
    CHECK (same.bitIdentical);
    CHECK_THAT (same.maxDiffDbfs, WithinAbs (-300.0, 1.0e-9));

    second[2] += 1.0e-5f;
    const auto different = sonolint::measureDeterminism (first, second);
    CHECK_FALSE (different.bitIdentical);
    CHECK_THAT (different.maxDiffDbfs, WithinAbs (-100.0, 0.05));
}

TEST_CASE ("determinism: renders of different length are not identical")
{
    const std::vector<float> first { 0.5f, 0.5f }, second { 0.5f, 0.5f, 0.5f };
    CHECK_FALSE (sonolint::measureDeterminism (first, second).bitIdentical);
}

TEST_CASE ("block invariance: the peak and the RMS of the difference between two renders")
{
    const std::vector<float> constantBlocks (1000, 0.5f);
    auto randomBlocks = constantBlocks;

    const auto same = sonolint::measureBlockInvariance (constantBlocks, randomBlocks);
    CHECK_THAT (same.maxDiffDbfs, WithinAbs (-300.0, 1.0e-9));
    CHECK_THAT (same.rmsDiffDbfs, WithinAbs (-300.0, 1.0e-9));

    // One sample in a thousand is off by 0.01: -40 dB at its peak, and 30 dB less on average.
    randomBlocks[500] += 0.01f;
    const auto different = sonolint::measureBlockInvariance (constantBlocks, randomBlocks);
    CHECK_THAT (different.maxDiffDbfs, WithinAbs (-40.0, 0.01));
    CHECK_THAT (different.rmsDiffDbfs, WithinAbs (-70.0, 0.01));
}

TEST_CASE ("cpu: the median render, as a fraction of real time, and its slowest blocks")
{
    // Five renders of 2 s of audio in 100 blocks each. Their totals are 10, 30, 20, 50 and 40 ms.
    std::vector<std::vector<double>> renders;

    for (const auto total : { 0.010, 0.030, 0.020, 0.050, 0.040 })
        renders.emplace_back (100, total / 100.0);

    renders[1][99] += 0.001;   // the median render (30 ms) has one block that takes 1 ms longer

    const auto cpu = sonolint::measureCpu (renders, 2.0);
    CHECK_THAT (cpu.realtimeFactor, WithinAbs (0.031 / 2.0, 1.0e-12));
    CHECK_THAT (cpu.corePercent, WithinAbs (1.55, 1.0e-9));
    CHECK_THAT (cpu.p95BlockUs, WithinAbs (300.0, 1.0e-6));   // 95 of its 100 blocks take 300 microseconds or less
}
