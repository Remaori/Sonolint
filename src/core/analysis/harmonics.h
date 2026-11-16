#pragma once

#include <array>
#include <optional>
#include <span>

namespace sonolint
{
struct HarmonicsResult
{
    double f0Hz = 0.0;
    double thdDb = 0.0;    // harmonics 2 to 10 over the fundamental
    double thdnDb = 0.0;   // everything from 20 Hz to 20 kHz except the fundamental, over the fundamental
    std::array<std::optional<double>, 9> hDbc;   // H2 to H10 in dBc; no value when out of band
};

// `window` is the analysis window of one channel that carries a sine at f0Hz.
// Throws Error (config) if the window is too short for the FFT.
HarmonicsResult measureHarmonics (std::span<const float> window, double sampleRate, double f0Hz);
} // namespace sonolint
