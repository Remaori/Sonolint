#include "core/error.h"
#include "core/io/wav.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE ("a WAV file written by writeWav reads back with the same samples")
{
    sonolint::AudioBuffer written;
    written.sampleRate = 44100.0;
    written.channels = { { 0.0f, 0.25f, -1.0f, 1.0e-7f, 1.5f }, { 1.0f, -0.5f, 0.0f, 0.125f, -2.0f } };

    const auto path = std::filesystem::temp_directory_path() / "sonolint_wav_test.wav";
    sonolint::writeWav (path, written);
    const auto read = sonolint::readWav (path);
    std::filesystem::remove (path);

    CHECK (read.sampleRate == 44100.0);
    CHECK (read.channels == written.channels);   // 32-bit float, so bit for bit
}

TEST_CASE ("reading a WAV file that doesn't exist is a configuration error")
{
    try
    {
        sonolint::readWav ("this/file/does/not/exist.wav");
        FAIL ("a missing file must be rejected");
    }
    catch (const sonolint::Error& e)
    {
        CHECK (e.code == sonolint::ErrorCode::config);
    }
}
