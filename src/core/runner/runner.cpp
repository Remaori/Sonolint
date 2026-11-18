#include "core/runner/runner.h"

#include "core/analysis/aliasing.h"
#include "core/analysis/block_invariance.h"
#include "core/analysis/cpu.h"
#include "core/analysis/determinism.h"
#include "core/analysis/frequency_response.h"
#include "core/analysis/harmonics.h"
#include "core/analysis/latency.h"
#include "core/analysis/level.h"
#include "core/analysis/silence.h"
#include "core/analysis/spectrum.h"
#include "core/analysis/true_peak.h"
#include "core/compare/compare.h"
#include "core/error.h"
#include "core/io/path_text.h"
#include "core/signals/generators.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <span>
#include <sstream>
#include <thread>

namespace sonolint
{
namespace
{
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr uint64_t kRandomBlocksSeed = 12345;
constexpr int kCpuRenders = 5;

size_t toSamples (double milliseconds, double sampleRate)
{
    return (size_t) std::llround (milliseconds * sampleRate / 1000.0);
}

//==============================================================================
json toJson (const LevelResult& r)
{
    return { { "peak_dbfs", r.peakDbfs }, { "rms_dbfs", r.rmsDbfs }, { "gain_db", r.gainDb }, { "dc", r.dc } };
}

json toJson (const TruePeakResult& r)
{
    return { { "true_peak_dbtp", r.truePeakDbtp }, { "sample_peak_dbfs", r.samplePeakDbfs } };
}

json toJson (const HarmonicsResult& r)
{
    auto harmonics = json::array();

    for (const auto& harmonic : r.hDbc)
        harmonics.push_back (harmonic ? json (*harmonic) : json());

    return { { "f0_hz", r.f0Hz }, { "thd_db", r.thdDb }, { "thdn_db", r.thdnDb }, { "h_dbc", harmonics } };
}

json toJson (const AliasingResult& r)
{
    return { { "aliasing_dbc", r.aliasingDbc }, { "components", r.components }, { "skipped", r.skipped } };
}

json toJson (const FrequencyResponseResult& r)
{
    return { { "freqs_hz", r.freqsHz }, { "magnitude_db", r.magnitudeDb }, { "phase_deg", r.phaseDeg }, { "gain_1k_db", r.gain1kDb } };
}

json toJson (const LatencyResult& r)
{
    return { { "reported_samples", r.reportedSamples }, { "measured_samples", r.measuredSamples }, { "mismatch_samples", r.mismatchSamples } };
}

json toJson (const SilenceResult& r)
{
    return { { "noise_floor_dbfs", r.noiseFloorDbfs }, { "peak_dbfs", r.peakDbfs } };
}

json toJson (const DeterminismResult& r)
{
    return { { "bit_identical", r.bitIdentical }, { "max_diff_dbfs", r.maxDiffDbfs } };
}

json toJson (const BlockInvarianceResult& r)
{
    return { { "max_diff_dbfs", r.maxDiffDbfs }, { "rms_diff_dbfs", r.rmsDiffDbfs } };
}

json toJson (const CpuResult& r)
{
    return { { "realtime_factor", r.realtimeFactor }, { "core_percent", r.corePercent }, { "p95_block_us", r.p95BlockUs } };
}

// The value reported for something measured on every channel: the worst channel,
// which is the highest number (for a yes/no value, "no"). Curves go point by point.
json worst (const std::vector<json>& channels)
{
    const auto& first = channels.front();

    if (first.is_object() || first.is_array())
    {
        auto result = first;

        for (auto item = result.begin(); item != result.end(); ++item)
        {
            std::vector<json> values;

            for (const auto& channel : channels)
                values.push_back (first.is_object() ? channel.at (item.key()) : channel.at ((size_t) (item - result.begin())));

            *item = worst (values);
        }

        return result;
    }

    auto result = first;

    for (const auto& value : channels)
    {
        if (value.is_null())
            continue;

        if (result.is_null())
            result = value;
        else if (value.is_boolean())
            result = result.get<bool>() && value.get<bool>();
        else if (value.get<double>() > result.get<double>())
            result = value;
    }

    return result;
}

// A metric group measured per channel: the worst channel's values plus each channel's own.
template <typename Measure>
json perChannel (size_t numChannels, Measure&& measure)
{
    std::vector<json> channels;

    for (size_t channel = 0; channel < numChannels; ++channel)
        channels.push_back (toJson (measure (channel)));

    auto group = worst (channels);
    group["per_channel"] = channels;
    return group;
}

//==============================================================================
json toJson (const Expectation& rule)
{
    switch (rule.kind)
    {
        case Expectation::Kind::approx:   return { { "approx", rule.value }, { "tol", rule.other } };
        case Expectation::Kind::min:      return { { "min", rule.value } };
        case Expectation::Kind::max:      return { { "max", rule.value } };
        case Expectation::Kind::between:  return { { "between", { rule.value, rule.other } } };
        case Expectation::Kind::equals:   break;
    }

    return { { "equals", rule.flag ? json (*rule.flag) : json (rule.value) } };
}

// A curve passes when every one of its points does; points without a value don't count.
bool holds (const Expectation& rule, const json& actual)
{
    if (actual.is_array())
        return std::all_of (actual.begin(), actual.end(), [&] (const json& point) { return point.is_null() || holds (rule, point); });

    if (rule.flag)
        return actual.is_boolean() && actual.get<bool>() == *rule.flag;

    if (! actual.is_number())
        return false;

    const auto value = actual.get<double>();

    switch (rule.kind)
    {
        case Expectation::Kind::approx:   return std::abs (value - rule.value) <= rule.other;
        case Expectation::Kind::min:      return value >= rule.value;
        case Expectation::Kind::max:      return value <= rule.value;
        case Expectation::Kind::between:  return value >= rule.value && value <= rule.other;
        case Expectation::Kind::equals:   break;
    }

    return value == rule.value;
}

bool measures (const Scenario& scenario, const std::string& group)
{
    return std::find (scenario.metrics.begin(), scenario.metrics.end(), group) != scenario.metrics.end();
}

// The rules of the scenario plus the ones Sonolint applies by itself to what is measured.
std::map<std::string, Expectation> withDefaults (const Scenario& scenario)
{
    auto expect = scenario.expect;

    const auto add = [&] (const std::string& group, const std::string& path, Expectation rule)
    {
        rule.isDefault = true;

        if (measures (scenario, group))
            expect.try_emplace (path, rule);   // a rule written in the config wins
    };

    add ("latency", "latency.mismatch_samples", { Expectation::Kind::between, -1.0, 1.0, {}, false });
    add ("determinism", "determinism.bit_identical", { Expectation::Kind::equals, 0.0, 0.0, true, false });
    add ("block_invariance", "block_invariance.max_diff_dbfs", { Expectation::Kind::max, -120.0, 0.0, {}, false });
    return expect;
}

json describe (const Scenario& scenario)
{
    auto params = json::object();

    for (const auto& param : scenario.params)
        params[param.key] = std::holds_alternative<double> (param.value) ? json (std::get<double> (param.value))
                                                                         : json { { "text", std::get<std::string> (param.value) } };

    const auto& s = scenario.signal;
    json signal { { "type", s.typeName } };

    switch (s.type)
    {
        case SignalType::sine:
            signal.update ({ { "freq_hz", s.freqHz }, { "level_dbfs", s.levelDbfs }, { "duration_s", s.durationS },
                             { "phase_deg", s.phaseDeg }, { "snap", s.snap } });
            break;

        case SignalType::steppedSine:
            signal.update ({ { "points", s.points }, { "level_dbfs", s.levelDbfs } });

            if (s.stepS)
                signal["step_s"] = *s.stepS;

            break;

        case SignalType::impulse:
            signal.update ({ { "amplitude", s.amplitude }, { "position_ms", s.positionMs }, { "duration_s", s.durationS } });
            break;

        case SignalType::whiteNoise:
        case SignalType::pinkNoise:
            signal.update ({ { "level_dbfs", s.levelDbfs }, { "duration_s", s.durationS }, { "seed", s.seed } });
            break;

        case SignalType::silence:
            signal["duration_s"] = s.durationS;
            break;

        case SignalType::file:
            signal["path"] = toUtf8 (s.path);
            break;
    }

    json config { { "sample_rate", scenario.sampleRate }, { "block_size", scenario.blockSize }, { "channels", scenario.channels },
                  { "warmup_ms", scenario.warmupMs },     { "tail_ms", scenario.tailMs },       { "non_realtime", scenario.nonRealtime },
                  { "skip_ms", scenario.skipMs },         { "timeout_s", scenario.timeoutS },   { "params", params },
                  { "signal", signal },                   { "metrics", scenario.metrics } };

    if (scenario.state)
        config["state"] = toUtf8 (*scenario.state);

    return config;
}

//==============================================================================
// Everything the metrics of one scenario need.
struct Measurement
{
    const Scenario& scenario;
    Renderer& renderer;
    Signal signal;
    RenderRequest request;
    RenderResult main;                         // the render with constant blocks
    std::vector<std::vector<float>> aligned;   // its output without the reported latency
    size_t skip = 0;

    // The part of a segment that gets analysed: without its first `skip` samples and
    // only as far as the aligned output reaches.
    std::pair<size_t, size_t> range (const Segment& segment) const
    {
        const auto start = segment.start + skip;
        const auto end   = std::min (segment.start + segment.length, aligned.front().size());

        if (end <= start)
            throw Error (ErrorCode::config, "nothing is left to analyse after skipping " + std::to_string ((long long) scenario.skipMs)
                                                + " ms and the plugin's latency; make the signal or tail_ms longer");

        return { start, end - start };
    }

    std::span<const float> window (const std::vector<float>& channel) const
    {
        const auto [start, length] = range (signal.segments.front());
        return std::span (channel).subspan (start, length);
    }

    json measure (const std::string& group)
    {
        const auto numChannels = aligned.size();
        const auto sampleRate  = scenario.sampleRate;
        const auto& input      = signal.audio.channels;
        const auto& segment    = signal.segments.front();

        if (group == "level")
            return perChannel (numChannels, [&] (size_t c) { return measureLevel (window (input[c]), window (aligned[c])); });

        if (group == "true_peak")
            return perChannel (numChannels, [&] (size_t c)
            {
                const auto [start, length] = range (segment);
                return measureTruePeak (aligned[c], start, length, sampleRate);
            });

        if (group == "harmonics")
            return perChannel (numChannels, [&] (size_t c) { return measureHarmonics (window (aligned[c]), sampleRate, segment.freqHz); });

        if (group == "aliasing")
            return perChannel (numChannels, [&] (size_t c) { return measureAliasing (window (aligned[c]), sampleRate, segment.freqHz); });

        if (group == "frequency_response")
            return perChannel (numChannels, [&] (size_t c)
            {
                return measureFrequencyResponse (input[c], aligned[c], signal.segments, sampleRate, skip);
            });

        if (group == "latency")
            return perChannel (numChannels, [&] (size_t c)
            {
                return measureLatency (input[c], main.output.channels[c], main.reportedLatencySamples);
            });

        if (group == "silence")
            return perChannel (numChannels, [&] (size_t c) { return measureSilence (window (aligned[c])); });

        if (group == "determinism")
        {
            const auto again = renderer.render (request);
            return perChannel (numChannels, [&] (size_t c) { return measureDeterminism (main.output.channels[c], again.output.channels.at (c)); });
        }

        if (group == "block_invariance")
        {
            auto randomBlocks = request;
            randomBlocks.blocks.randomSeed = kRandomBlocksSeed;
            const auto other = renderer.render (randomBlocks);

            return perChannel (numChannels, [&] (size_t c)
            {
                return measureBlockInvariance (main.output.channels[c], other.output.channels.at (c));
            });
        }

        // cpu: the render we already have and four more.
        std::vector<std::vector<double>> blockSeconds { main.processSecondsPerBlock };

        while ((int) blockSeconds.size() < kCpuRenders)
            blockSeconds.push_back (renderer.render (request).processSecondsPerBlock);

        return toJson (measureCpu (blockSeconds, (double) main.output.numSamples() / sampleRate));
    }
};

json runScenario (const Scenario& scenario, Renderer& renderer)
{
    const auto snap = measures (scenario, "harmonics") || measures (scenario, "aliasing");

    Measurement m { scenario, renderer, makeSignal (scenario.signal, scenario.sampleRate, scenario.channels, scenario.skipMs, snap),
                    {}, {}, {}, toSamples (scenario.skipMs, scenario.sampleRate) };

    m.request.sampleRate         = scenario.sampleRate;
    m.request.channels           = scenario.channels;
    m.request.blocks.maxBlockSize = scenario.blockSize;
    m.request.params             = scenario.params;
    m.request.stateFile          = scenario.state;
    m.request.nonRealtime        = scenario.nonRealtime;
    m.request.warmupSamples      = (int) toSamples (scenario.warmupMs, scenario.sampleRate);
    m.request.input              = m.signal.audio;
    m.request.tailSamples        = (int) toSamples (scenario.tailMs, scenario.sampleRate);

    m.main = renderer.render (m.request);

    if (m.main.output.channels.size() != m.signal.audio.channels.size())
        throw Error (ErrorCode::internal, "the renderer returned " + std::to_string (m.main.output.channels.size()) + " channels for "
                                              + std::to_string (m.signal.audio.channels.size()));

    json result { { "reported_latency_samples", m.main.reportedLatencySamples } };

    if (! m.main.warnings.empty())
        result["warnings"] = m.main.warnings;

    // A NaN or an infinity anywhere in the output always fails, whatever was asked.
    for (const auto& channel : m.main.output.channels)
        if (! std::all_of (channel.begin(), channel.end(), [] (float sample) { return std::isfinite (sample); }))
            throw Error (ErrorCode::config, "the output has NaN or infinite samples");

    // Aligned: without the latency the plugin reports, as a DAW would compensate it.
    const auto latency = (size_t) std::max (0, m.main.reportedLatencySamples);

    for (const auto& channel : m.main.output.channels)
        m.aligned.emplace_back (channel.begin() + (std::ptrdiff_t) std::min (latency, channel.size()), channel.end());

    auto& metrics = result["metrics"] = json::object();

    for (const auto& group : scenario.metrics)
        metrics[group] = m.measure (group);

    if (measures (scenario, "aliasing") && metrics["aliasing"]["skipped"].get<int>() > metrics["aliasing"]["components"].get<int>())
        result["warnings"].push_back ("more than half of the folded harmonics land on other content, so the aliasing figure says little: "
                                      "use a frequency that doesn't divide the sample rate, such as 9973 Hz");

    auto& assertions = result["assertions"] = json::array();
    auto passed = true;

    for (const auto& [path, rule] : withDefaults (scenario))
    {
        const auto dot = path.find ('.');
        const auto& actual = metrics.at (path.substr (0, dot)).at (path.substr (dot + 1));
        const auto ok = holds (rule, actual);
        passed = passed && ok;

        assertions.push_back ({ { "metric", path }, { "rule", toJson (rule) }, { "actual", actual }, { "passed", ok }, { "default", rule.isDefault } });
    }

    result["status"] = passed ? "pass" : "assertion_failed";
    return result;
}

//==============================================================================
// Calls a function from another thread when a scenario takes longer than it may.
class Watchdog
{
public:
    explicit Watchdog (std::function<void()> onTimeoutToCall) : onTimeout (std::move (onTimeoutToCall)), thread ([this] { run(); }) {}

    ~Watchdog()
    {
        {
            const std::lock_guard lock (mutex);
            quit = true;
        }

        changed.notify_all();
        thread.join();
    }

    void arm (double seconds)
    {
        {
            const std::lock_guard lock (mutex);
            deadline = Clock::now() + std::chrono::duration_cast<Clock::duration> (std::chrono::duration<double> (seconds));
            armed = true;
        }

        changed.notify_all();
    }

    void disarm()
    {
        const std::lock_guard lock (mutex);
        armed = false;
    }

private:
    void run()
    {
        std::unique_lock lock (mutex);

        while (! quit)
        {
            if (! armed)
            {
                changed.wait (lock);
            }
            else if (changed.wait_until (lock, deadline) == std::cv_status::timeout && armed && Clock::now() >= deadline)
            {
                armed = false;
                lock.unlock();
                onTimeout();
                lock.lock();
            }
        }
    }

    std::function<void()> onTimeout;
    std::mutex mutex;
    std::condition_variable changed;
    Clock::time_point deadline;
    bool armed = false, quit = false;
    std::thread thread;   // last: it starts running in the constructor
};

} // namespace

//==============================================================================
Signal makeSignal (const SignalSpec& spec, double sampleRate, int channels, double skipMs, bool snapToAnalysisBin)
{
    Signal signal;

    switch (spec.type)
    {
        case SignalType::sine:
        {
            auto freqHz = spec.freqHz;

            if (snapToAnalysisBin && spec.snap)
            {
                // The FFT analyses the segment after the skipped part: its length decides where the bins are.
                const auto length = makeSine (sampleRate, freqHz, spec.levelDbfs, spec.durationS, spec.phaseDeg).segments.front().length;
                const auto skip   = std::min (toSamples (skipMs, sampleRate), length);
                freqHz = snapToBin (freqHz, sampleRate, analysisFftSize (length - skip));
            }

            signal = makeSine (sampleRate, freqHz, spec.levelDbfs, spec.durationS, spec.phaseDeg);
            break;
        }

        case SignalType::steppedSine:  signal = makeSteppedSine (sampleRate, spec.points, spec.levelDbfs, skipMs, spec.stepS); break;
        case SignalType::impulse:      signal = makeImpulse (sampleRate, spec.amplitude, spec.positionMs, spec.durationS); break;
        case SignalType::whiteNoise:   signal = makeWhiteNoise (sampleRate, spec.levelDbfs, spec.durationS, spec.seed); break;
        case SignalType::pinkNoise:    signal = makePinkNoise (sampleRate, spec.levelDbfs, spec.durationS, spec.seed); break;
        case SignalType::silence:      signal = makeSilence (sampleRate, spec.durationS); break;
        case SignalType::file:         signal = makeFromFile (spec.path, sampleRate); break;
    }

    auto& audio = signal.audio.channels;

    if (audio.size() == 1)
    {
        const auto mono = audio.front();
        audio.assign ((size_t) channels, mono);   // the same signal goes to every channel
    }
    else if ((int) audio.size() != channels)
    {
        throw Error (ErrorCode::config, toUtf8 (spec.path) + " has " + std::to_string (audio.size()) + " channels and the scenario runs with "
                                            + std::to_string (channels));
    }

    return signal;
}

json runScenarios (const std::vector<Scenario>& scenarios, const PluginInfo& plugin, Renderer& renderer, const RunOptions& options)
{
    // Every parameter must exist before anything is rendered.
    for (const auto& scenario : scenarios)
    {
        for (const auto& param : scenario.params)
        {
            try
            {
                findParam (plugin, param.key);
            }
            catch (const Error& e)
            {
                throw Error (ErrorCode::config, "scenario \"" + scenario.id + "\": " + e.what());
            }
        }
    }

    json results { { "schema", "sonolint.results/v1" },
                   { "plugin", { { "name", plugin.name }, { "vendor", plugin.vendor }, { "version", plugin.version } } },
                   { "scenarios", json::array() } };

    std::mutex resultsMutex;   // the watchdog reads `results` from its own thread
    json running;              // the scenario in progress, as the watchdog would report it

    Watchdog watchdog ([&]
    {
        json partial;

        {
            const std::lock_guard lock (resultsMutex);
            partial = results;
            partial["scenarios"].push_back (running);
        }

        partial["summary"] = summarise (partial["scenarios"]);

        if (options.onTimeout)
            options.onTimeout (partial);
    });

    for (size_t i = 0; i < scenarios.size(); ++i)
    {
        const auto& scenario = scenarios[i];
        json entry { { "id", scenario.id }, { "config", describe (scenario) } };

        if (! scenario.description.empty())
            entry["description"] = scenario.description;

        {
            const std::lock_guard lock (resultsMutex);
            std::ostringstream limit;
            limit << scenario.timeoutS;

            running = entry;
            running.update ({ { "status", "error" }, { "error", "timed out after " + limit.str() + " s" } });
        }

        const auto started = Clock::now();
        watchdog.arm (scenario.timeoutS);

        try
        {
            entry.update (runScenario (scenario, renderer));
        }
        catch (const Error& e)
        {
            if (e.code == ErrorCode::pluginLoad)
                throw;

            entry.update ({ { "status", "error" }, { "error", e.what() } });
        }

        watchdog.disarm();
        entry["seconds"] = std::chrono::duration<double> (Clock::now() - started).count();

        if (options.log)
            options.log ("[" + std::to_string (i + 1) + "/" + std::to_string (scenarios.size()) + "] " + scenario.id + ": "
                         + entry["status"].get<std::string>());

        const std::lock_guard lock (resultsMutex);
        results["scenarios"].push_back (std::move (entry));
    }

    results["summary"] = summarise (results["scenarios"]);
    return results;
}
} // namespace sonolint
