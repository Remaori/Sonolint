#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace sonolint
{
// xoshiro256** by Blackman and Vigna, seeded through SplitMix64 as its authors
// recommend. Our own generator and never <random>'s distributions, which give
// different numbers on each standard library.
class Prng
{
public:
    explicit Prng (uint64_t seed)
    {
        for (auto& word : state)
        {
            seed += 0x9e3779b97f4a7c15ull;
            auto z = seed;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            word = z ^ (z >> 31);
        }
    }

    uint64_t next()
    {
        const auto result = rotl (state[1] * 5, 7) * 9;
        const auto t = state[1] << 17;

        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];
        state[2] ^= t;
        state[3] = rotl (state[3], 45);

        return result;
    }

    // In (0, 1]: never 0, so its logarithm is finite.
    double uniform() { return (double) ((next() >> 11) + 1) * 0x1.0p-53; }

    // Mean 0 and variance 1, by Box-Muller. Each value uses two uniform ones.
    double gaussian()
    {
        const auto radius = std::sqrt (-2.0 * std::log (uniform()));
        return radius * std::cos (2.0 * std::numbers::pi * uniform());
    }

private:
    static uint64_t rotl (uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t state[4];
};
} // namespace sonolint
