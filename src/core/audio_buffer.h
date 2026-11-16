#pragma once

#include <cstddef>
#include <vector>

namespace sonolint
{
// Audio as it goes into and comes out of a plugin: 32-bit float, one vector per
// channel, all of the same length. Analysis converts to double.
struct AudioBuffer
{
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels;

    size_t numSamples() const { return channels.empty() ? 0 : channels.front().size(); }
};
} // namespace sonolint
