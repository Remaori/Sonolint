#pragma once

#include "core/audio_buffer.h"

#include <cstddef>
#include <vector>

namespace sonolint
{
// A stretch of a test signal that the analyzers measure. It never includes the
// fade-out at the end of the signal.
struct Segment
{
    size_t start = 0;
    size_t length = 0;
    double freqHz = 0.0;   // 0 when the stretch has no single frequency (noise, impulse, file)
};

struct Signal
{
    AudioBuffer audio;
    std::vector<Segment> segments;
};
} // namespace sonolint
