#pragma once

#include "core/runner/renderer.h"
#include "core/signals/signal.h"

#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

namespace test
{
// A Renderer that runs a function over each channel instead of hosting a plugin.
class FakeRenderer : public sonolint::Renderer
{
public:
    using Process = std::function<std::vector<float> (const std::vector<float>&)>;

    explicit FakeRenderer (Process processToUse, int reportedLatencyToUse = 0)
        : process (std::move (processToUse)), reportedLatency (reportedLatencyToUse)
    {
    }

    sonolint::RenderResult render (const sonolint::RenderRequest& request) override
    {
        sonolint::RenderResult result;
        result.output.sampleRate = request.sampleRate;
        result.reportedLatencySamples = reportedLatency;

        for (auto channel : request.input.channels)
        {
            channel.resize (channel.size() + (size_t) request.tailSamples, 0.0f);
            result.output.channels.push_back (process (channel));
        }

        return result;
    }

private:
    Process process;
    int reportedLatency;
};

// The request a runner would make for this signal.
inline sonolint::RenderRequest requestFor (const sonolint::Signal& signal, int tailSamples = 0)
{
    sonolint::RenderRequest request;
    request.sampleRate = signal.audio.sampleRate;
    request.channels = (int) signal.audio.channels.size();
    request.input = signal.audio;
    request.tailSamples = tailSamples;
    return request;
}

inline FakeRenderer::Process delay (size_t samples)
{
    return [samples] (const std::vector<float>& input)
    {
        std::vector<float> output (input.size(), 0.0f);

        for (auto n = samples; n < input.size(); ++n)
            output[n] = input[n - samples];

        return output;
    };
}

// samples + 0.5: the mean of two neighbours sits halfway between them.
inline FakeRenderer::Process delayPlusHalf (size_t samples)
{
    return [samples] (const std::vector<float>& input)
    {
        std::vector<float> output (input.size(), 0.0f);

        for (auto n = samples + 1; n < input.size(); ++n)
            output[n] = 0.5f * (input[n - samples] + input[n - samples - 1]);

        return output;
    };
}

// Coefficients of a first-order low-pass (bilinear transform, cutoff pre-warped):
// y[n] = b * (x[n] + x[n - 1]) - a * y[n - 1].
struct OnePole
{
    OnePole (double cutoffHz, double sampleRate)
    {
        const auto k = std::tan (std::numbers::pi * cutoffHz / sampleRate);
        b = k / (1.0 + k);
        a = (k - 1.0) / (k + 1.0);
    }

    double b, a;
};

inline FakeRenderer::Process lowPass (double cutoffHz, double sampleRate)
{
    return [filter = OnePole (cutoffHz, sampleRate)] (const std::vector<float>& input)
    {
        std::vector<float> output (input.size());
        double previousInput = 0.0, previousOutput = 0.0;

        for (size_t n = 0; n < input.size(); ++n)
        {
            const auto y = filter.b * (input[n] + previousInput) - filter.a * previousOutput;
            previousInput = input[n];
            previousOutput = y;
            output[n] = (float) y;
        }

        return output;
    };
}
} // namespace test
