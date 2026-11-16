#pragma once

#include <algorithm>
#include <cmath>

namespace sonolint
{
// Full scale is 1.0. The argument of every logarithm is floored at 1e-15, so no
// result is ever -inf: silence is -300 dB as an amplitude and -150 dB as a power.
inline double amplitudeToDb (double amplitude) { return 20.0 * std::log10 (std::max (std::abs (amplitude), 1.0e-15)); }
inline double powerToDb (double power) { return 10.0 * std::log10 (std::max (power, 1.0e-15)); }

// A ratio of powers in dB that stays finite when the denominator is 0 (a silent output).
inline double powerRatioToDb (double numerator, double denominator) { return powerToDb (numerator / std::max (denominator, 1.0e-300)); }
} // namespace sonolint
