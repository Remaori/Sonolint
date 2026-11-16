#pragma once

#include "core/audio_buffer.h"
#include "core/runner/plugin_info.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sonolint
{
struct ParamValue
{
    std::string key;                           // parameter name; falls back to the hosted ID
    std::variant<double, std::string> value;   // normalized 0..1, or text ("-6 dB")
};

struct BlockSchedule
{
    int maxBlockSize = 512;
    std::optional<uint64_t> randomSeed;   // set: blocks of random sizes from 1 to maxBlockSize
};

struct RenderRequest
{
    double sampleRate = 48000.0;
    int channels = 2;
    BlockSchedule blocks;
    std::vector<ParamValue> params;
    std::optional<std::filesystem::path> stateFile;   // binary state from getStateInformation
    bool nonRealtime = false;                          // AudioProcessor::setNonRealtime
    int warmupSamples = 0;                             // silence processed and discarded first
    AudioBuffer input;
    int tailSamples = 0;                               // silence appended after the input
};

struct RenderResult
{
    AudioBuffer output;   // untrimmed: as long as the input plus the tail
    int reportedLatencySamples = 0;
    std::vector<double> processSecondsPerBlock;
    PluginInfo info;
};

// The only way the core gets audio out of a plugin. sonolint-host implements it with
// JUCE; the unit tests, with plain functions.
class Renderer
{
public:
    virtual ~Renderer() = default;

    // Every call uses a fresh plugin instance.
    virtual RenderResult render (const RenderRequest& request) = 0;
};
} // namespace sonolint
