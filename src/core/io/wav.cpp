#include "core/io/wav.h"

#include "core/error.h"
#include "core/io/path_text.h"

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

namespace sonolint
{
AudioBuffer readWav (const std::filesystem::path& path)
{
    unsigned int channels = 0, sampleRate = 0;
    drwav_uint64 frames = 0;

    // path.c_str() is wide on Windows: the narrow functions would misread non-ASCII paths.
   #ifdef _WIN32
    auto* data = drwav_open_file_and_read_pcm_frames_f32_w (path.c_str(), &channels, &sampleRate, &frames, nullptr);
   #else
    auto* data = drwav_open_file_and_read_pcm_frames_f32 (path.c_str(), &channels, &sampleRate, &frames, nullptr);
   #endif

    if (data == nullptr)
        throw Error (ErrorCode::config, "could not read the WAV file " + toUtf8 (path));

    AudioBuffer buffer;
    buffer.sampleRate = sampleRate;
    buffer.channels.assign (channels, std::vector<float> ((size_t) frames));

    for (size_t frame = 0; frame < (size_t) frames; ++frame)
        for (size_t channel = 0; channel < channels; ++channel)
            buffer.channels[channel][frame] = data[frame * channels + channel];

    drwav_free (data, nullptr);
    return buffer;
}

void writeWav (const std::filesystem::path& path, const AudioBuffer& buffer)
{
    const auto channels = buffer.channels.size();
    const auto frames   = buffer.numSamples();
    std::vector<float> interleaved (frames * channels);

    for (size_t frame = 0; frame < frames; ++frame)
        for (size_t channel = 0; channel < channels; ++channel)
            interleaved[frame * channels + channel] = buffer.channels[channel][frame];

    drwav_data_format format {};
    format.container     = drwav_container_riff;
    format.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels      = (drwav_uint32) channels;
    format.sampleRate    = (drwav_uint32) buffer.sampleRate;
    format.bitsPerSample = 32;

    drwav wav;

   #ifdef _WIN32
    const auto opened = drwav_init_file_write_w (&wav, path.c_str(), &format, nullptr);
   #else
    const auto opened = drwav_init_file_write (&wav, path.c_str(), &format, nullptr);
   #endif

    if (! opened)
        throw Error (ErrorCode::config, "could not write the WAV file " + toUtf8 (path));

    const auto written = drwav_write_pcm_frames (&wav, frames, interleaved.data());
    drwav_uninit (&wav);

    if (written != frames)
        throw Error (ErrorCode::config, "could not write the whole WAV file " + toUtf8 (path));
}
} // namespace sonolint
