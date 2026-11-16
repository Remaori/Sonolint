#pragma once

#include "core/signals/signal.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace sonolint
{
// Test signals. The same arguments always give the same samples. Every generator
// makes one channel, except makeFromFile, which keeps the channels of the file.
//
// levelDbfs is the peak level for sines and the RMS level for noise. Sines and noise
// fade in and out over 5 ms; impulses, silence and files don't.
// Bad arguments throw Error (config).

Signal makeSine (double sampleRate, double freqHz, double levelDbfs, double durationS = 2.0, double phaseDeg = 0.0);

// One step per frequency, in order, as one continuous wave. Without stepS a step
// lasts 0.25 s or 50 cycles, whichever is longer, plus skipMs. Frequencies above
// 0.45 * sampleRate are left out.
Signal makeSteppedSine (double sampleRate, std::span<const double> freqsHz, double levelDbfs, double skipMs = 100.0,
                        std::optional<double> stepS = std::nullopt);

Signal makeImpulse (double sampleRate, double amplitude = 0.5, double positionMs = 50.0, double durationS = 1.0);

Signal makeWhiteNoise (double sampleRate, double levelDbfs, double durationS, uint64_t seed = 1);

// Gaussian white noise shaped by 1 / sqrt (f) in the frequency domain, from 20 Hz up.
Signal makePinkNoise (double sampleRate, double levelDbfs, double durationS, uint64_t seed = 1);

Signal makeSilence (double sampleRate, double durationS);

// The WAV file as it is. Its sample rate must be sampleRate: nothing is resampled.
Signal makeFromFile (const std::filesystem::path& path, double sampleRate);

// The points of a stepped sine: nominal frequencies from 20 Hz to 20 kHz.
std::vector<double> thirdOctaveFrequencies();
std::vector<double> octaveFrequencies();
} // namespace sonolint
