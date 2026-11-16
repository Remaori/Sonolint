#include "core/signals/generators.h"

#include "core/analysis/fft.h"
#include "core/error.h"
#include "core/io/path_text.h"
#include "core/io/wav.h"
#include "core/signals/prng.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace sonolint
{
namespace
{
constexpr double kFadeSeconds = 0.005;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

void check (bool ok, const std::string& message)
{
    if (! ok)
        throw Error (ErrorCode::config, message);
}

size_t toSamples (double seconds, double sampleRate)
{
    check (sampleRate > 0.0, "the sample rate must be positive");
    const auto samples = std::llround (seconds * sampleRate);
    check (samples > 0, "the signal would have no samples");
    return (size_t) samples;
}

double toAmplitude (double levelDbfs) { return std::pow (10.0, levelDbfs / 20.0); }

void checkFrequency (double freqHz, double sampleRate)
{
    check (freqHz > 0.0 && freqHz < 0.5 * sampleRate,
           "the frequency " + std::to_string (freqHz) + " Hz must be above 0 and below half the sample rate");
}

// Raised-cosine fade-in over the first samples and fade-out over the last ones.
// Returns how many samples each fade takes.
size_t applyFades (std::vector<double>& samples, double sampleRate)
{
    const auto fade = std::min (toSamples (kFadeSeconds, sampleRate), samples.size() / 2);

    for (size_t i = 0; i < fade; ++i)
    {
        const auto gain = 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / (double) fade);
        samples[i] *= gain;
        samples[samples.size() - 1 - i] *= gain;
    }

    return fade;
}

// Signals are computed in double and become float here, once.
Signal toSignal (const std::vector<double>& samples, double sampleRate, std::vector<Segment> segments)
{
    Signal signal;
    signal.audio.sampleRate = sampleRate;
    signal.audio.channels.emplace_back (samples.size());
    std::transform (samples.begin(), samples.end(), signal.audio.channels[0].begin(), [] (double sample) { return (float) sample; });
    signal.segments = std::move (segments);
    return signal;
}

// Appends `length` samples of a sine that starts at `phase`, and moves `phase` to
// where the next sample would be.
void appendSine (std::vector<double>& samples, size_t length, double amplitude, double freqHz, double sampleRate, double& phase)
{
    const auto step = kTwoPi * freqHz / sampleRate;

    for (size_t i = 0; i < length; ++i)
        samples.push_back (amplitude * std::sin (phase + step * (double) i));

    phase = std::fmod (phase + step * (double) length, kTwoPi);
}

// Gaussian noise with variance 1.
std::vector<double> gaussianNoise (size_t length, uint64_t seed)
{
    Prng prng (seed);
    std::vector<double> samples (length);

    for (auto& sample : samples)
        sample = prng.gaussian();

    return samples;
}

void setRms (std::vector<double>& samples, double rms)
{
    double sum = 0.0;

    for (const auto sample : samples)
        sum += sample * sample;

    const auto gain = rms / std::sqrt (sum / (double) samples.size());

    for (auto& sample : samples)
        sample *= gain;
}

// Noise fades like a sine does, and its segment leaves the fade-out outside.
Signal noiseSignal (std::vector<double> samples, double sampleRate, double levelDbfs)
{
    setRms (samples, toAmplitude (levelDbfs));
    const auto fade = applyFades (samples, sampleRate);
    return toSignal (samples, sampleRate, { { 0, samples.size() - fade, 0.0 } });
}
} // namespace

Signal makeSine (double sampleRate, double freqHz, double levelDbfs, double durationS, double phaseDeg)
{
    checkFrequency (freqHz, sampleRate);

    std::vector<double> samples;
    auto phase = phaseDeg * std::numbers::pi / 180.0;
    appendSine (samples, toSamples (durationS, sampleRate), toAmplitude (levelDbfs), freqHz, sampleRate, phase);

    const auto fade = applyFades (samples, sampleRate);
    return toSignal (samples, sampleRate, { { 0, samples.size() - fade, freqHz } });
}

Signal makeSteppedSine (double sampleRate, std::span<const double> freqsHz, double levelDbfs, double skipMs, std::optional<double> stepS)
{
    const auto amplitude = toAmplitude (levelDbfs);
    std::vector<double> samples;
    std::vector<Segment> segments;
    double phase = 0.0;

    for (const auto freqHz : freqsHz)
    {
        if (freqHz > 0.45 * sampleRate)
            continue;   // the frequency response isn't measured that close to Nyquist

        checkFrequency (freqHz, sampleRate);
        const auto length = toSamples (stepS.value_or (std::max (0.25, 50.0 / freqHz) + skipMs / 1000.0), sampleRate);
        segments.push_back ({ samples.size(), length, freqHz });
        appendSine (samples, length, amplitude, freqHz, sampleRate, phase);
    }

    check (! segments.empty(), "the stepped sine has no frequency below 0.45 times the sample rate");

    // The fade-out gets its own stretch after the last step, so no step is measured through it.
    appendSine (samples, toSamples (kFadeSeconds, sampleRate), amplitude, segments.back().freqHz, sampleRate, phase);
    applyFades (samples, sampleRate);

    return toSignal (samples, sampleRate, std::move (segments));
}

Signal makeImpulse (double sampleRate, double amplitude, double positionMs, double durationS)
{
    std::vector<double> samples (toSamples (durationS, sampleRate), 0.0);
    const auto position = std::llround (positionMs / 1000.0 * sampleRate);
    check (position >= 0 && (size_t) position < samples.size(), "the impulse position is outside the signal");

    samples[(size_t) position] = amplitude;
    return toSignal (samples, sampleRate, { { 0, samples.size(), 0.0 } });
}

Signal makeWhiteNoise (double sampleRate, double levelDbfs, double durationS, uint64_t seed)
{
    return noiseSignal (gaussianNoise (toSamples (durationS, sampleRate), seed), sampleRate, levelDbfs);
}

Signal makePinkNoise (double sampleRate, double levelDbfs, double durationS, uint64_t seed)
{
    const auto length = toSamples (durationS, sampleRate);
    auto spectrum = realFft (gaussianNoise (length, seed));

    // The whole signal in one FFT: 3 dB less power per octave, and nothing below 20 Hz.
    for (size_t k = 0; k < spectrum.size(); ++k)
    {
        const auto freqHz = (double) k * sampleRate / (double) length;
        spectrum[k] *= freqHz < 20.0 ? 0.0 : 1.0 / std::sqrt (freqHz);
    }

    return noiseSignal (inverseRealFft (spectrum, length), sampleRate, levelDbfs);
}

Signal makeSilence (double sampleRate, double durationS)
{
    const std::vector<double> samples (toSamples (durationS, sampleRate), 0.0);
    return toSignal (samples, sampleRate, { { 0, samples.size(), 0.0 } });
}

Signal makeFromFile (const std::filesystem::path& path, double sampleRate)
{
    Signal signal;
    signal.audio = readWav (path);

    check (signal.audio.sampleRate == sampleRate,
           toUtf8 (path) + " is at " + std::to_string ((long long) signal.audio.sampleRate) + " Hz and the scenario runs at "
               + std::to_string ((long long) sampleRate) + " Hz; Sonolint doesn't resample");
    check (signal.audio.numSamples() > 0, toUtf8 (path) + " has no audio");

    signal.segments = { { 0, signal.audio.numSamples(), 0.0 } };
    return signal;
}

std::vector<double> thirdOctaveFrequencies()
{
    return { 20.0, 25.0, 31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0, 315.0, 400.0, 500.0, 630.0,
             800.0, 1000.0, 1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0,
             16000.0, 20000.0 };
}

std::vector<double> octaveFrequencies()
{
    return { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
}
} // namespace sonolint
