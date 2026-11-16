#pragma once

#include "core/audio_buffer.h"

#include <filesystem>

namespace sonolint
{
// Reads a WAV file of any common sample format as float. Throws Error (config) if it
// can't be read.
AudioBuffer readWav (const std::filesystem::path& path);

// Writes 32-bit float WAV, which keeps the samples bit for bit.
void writeWav (const std::filesystem::path& path, const AudioBuffer& buffer);
} // namespace sonolint
