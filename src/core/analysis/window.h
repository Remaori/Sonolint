#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace sonolint
{
// 4-term Blackman-Harris window, periodic form. A sine that sits exactly on an FFT
// bin then leaks into the 3 bins on each side and nowhere else.
inline std::vector<double> blackmanHarris (size_t size)
{
    std::vector<double> window (size);

    for (size_t n = 0; n < size; ++n)
    {
        const auto x = 2.0 * std::numbers::pi * (double) n / (double) size;
        window[n] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x);
    }

    return window;
}
} // namespace sonolint
