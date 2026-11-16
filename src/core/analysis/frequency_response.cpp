#include "core/analysis/frequency_response.h"

#include "core/analysis/db.h"
#include "core/error.h"

#include <complex>
#include <numbers>
#include <string>

namespace sonolint
{
namespace
{
// Complex amplitude of the component at freqHz: (2 / M) * sum of x[n] * e^(-j 2 pi f n / fs),
// where n counts from the start of the signal so input and output share the phase reference.
std::complex<double> amplitudeAt (std::span<const float> samples, size_t firstIndex, double freqHz, double sampleRate)
{
    std::complex<double> sum;

    for (size_t i = 0; i < samples.size(); ++i)
        sum += (double) samples[i] * std::polar (1.0, -2.0 * std::numbers::pi * freqHz * (double) (firstIndex + i) / sampleRate);

    return 2.0 * sum / (double) samples.size();
}
} // namespace

FrequencyResponseResult measureFrequencyResponse (std::span<const float> input, std::span<const float> output,
                                                  std::span<const Segment> segments, double sampleRate, size_t skipSamples)
{
    FrequencyResponseResult result;

    for (const auto& segment : segments)
    {
        // The window: the step without its first skipSamples, inside both signals and
        // cut down to a whole number of cycles.
        const auto start = segment.start + skipSamples;
        const auto end   = std::min ({ segment.start + segment.length, input.size(), output.size() });
        const auto cycles = end > start ? std::floor ((double) (end - start) * segment.freqHz / sampleRate) : 0.0;

        if (cycles < 1.0)
            throw Error (ErrorCode::config, "the step at " + std::to_string (segment.freqHz)
                                                + " Hz is too short to measure after the skipped part");

        const auto length = (size_t) std::llround (cycles * sampleRate / segment.freqHz);
        const auto x = amplitudeAt (input.subspan (start, length), start, segment.freqHz, sampleRate);
        const auto y = amplitudeAt (output.subspan (start, length), start, segment.freqHz, sampleRate);

        // The phase of y / x, written without the division so a silent signal gives 0 and not NaN.
        auto phase = std::arg (y * std::conj (x)) * 180.0 / std::numbers::pi;

        if (! result.phaseDeg.empty())
            phase -= 360.0 * std::round ((phase - result.phaseDeg.back()) / 360.0);

        result.freqsHz.push_back (segment.freqHz);
        result.magnitudeDb.push_back (amplitudeToDb (std::abs (y)) - amplitudeToDb (std::abs (x)));
        result.phaseDeg.push_back (phase);
    }

    for (size_t i = 0; i < result.freqsHz.size(); ++i)
        if (i == 0 || std::abs (result.freqsHz[i] - 1000.0) < std::abs (result.freqsHz[i - 1] - 1000.0))
            result.gain1kDb = result.magnitudeDb[i];

    return result;
}
} // namespace sonolint
