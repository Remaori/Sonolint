#include "core/analysis/spectrum.h"
#include "core/error.h"
#include "core/report/summary.h"
#include "core/runner/runner.h"
#include "fake_renderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <limits>
#include <random>
#include <thread>

using Catch::Matchers::WithinAbs;
using json = nlohmann::json;

namespace
{
const sonolint::PluginInfo kPlugin { "Fake", "Acme", "1.0", { { 0, "Gain", "100", 0.5, "", "", "", 0, false },
                                                             { 1, "Mix", "101", 1.0, "", "", "", 0, false } } };

// The scenarios of a config with these lines inside `scenarios:`.
std::vector<sonolint::Scenario> scenarios (const std::string& yaml)
{
    return sonolint::parseConfig ("version: 1\nscenarios:\n" + yaml, "/repo").scenarios;
}

std::vector<float> scaled (const std::vector<float>& input, float gain)
{
    auto output = input;

    for (auto& sample : output)
        sample *= gain;

    return output;
}

// The first (or only) assertion on `metric` in a scenario's results.
const json& assertionOn (const json& scenario, const std::string& metric)
{
    for (const auto& assertion : scenario.at ("assertions"))
        if (assertion.at ("metric") == metric)
            return assertion;

    FAIL ("no assertion on " + metric);
    return scenario;
}
} // namespace

TEST_CASE ("runner: measures every channel and reports the worst one")
{
    // The right channel comes out 6 dB lower than the left one.
    test::LambdaRenderer renderer ([] (const sonolint::RenderRequest& request)
    {
        sonolint::RenderResult result;
        result.output = request.input;
        result.output.channels[1] = scaled (request.input.channels[1], 0.5f);

        for (auto& channel : result.output.channels)
            channel.resize (channel.size() + (size_t) request.tailSamples, 0.0f);

        return result;
    });

    const auto results = sonolint::runScenarios (scenarios ("  - id: gain\n"
                                                            "    signal: { type: sine, freq_hz: 1000, level_dbfs: -6 }\n"
                                                            "    metrics: [level]\n"
                                                            "    expect: { level.gain_db: { approx: 0.0, tol: 0.05 } }\n"),
                                                 kPlugin, renderer);

    CHECK (results["schema"] == "sonolint.results/v1");
    CHECK (results["plugin"]["name"] == "Fake");
    REQUIRE (results["scenarios"].size() == 1);

    const auto& scenario = results["scenarios"][0];
    CHECK (scenario["id"] == "gain");
    CHECK (scenario["status"] == "pass");
    CHECK (scenario["config"]["sample_rate"] == 48000.0);
    CHECK (scenario["config"]["signal"]["type"] == "sine");

    const auto& level = scenario["metrics"]["level"];
    CHECK_THAT (level["gain_db"].get<double>(), WithinAbs (0.0, 1.0e-6));   // the worst channel is the highest value
    CHECK_THAT (level["per_channel"][0]["gain_db"].get<double>(), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (level["per_channel"][1]["gain_db"].get<double>(), WithinAbs (-6.0206, 1.0e-3));
    CHECK_THAT (level["peak_dbfs"].get<double>(), WithinAbs (-6.0, 1.0e-3));

    CHECK (assertionOn (scenario, "level.gain_db")["passed"] == true);
    CHECK (results["summary"] == json { { "pass", 1 }, { "assertion_failed", 0 }, { "error", 0 } });
    CHECK_FALSE (sonolint::hasFailures (results));
}

TEST_CASE ("runner: a failed expectation fails the scenario and says what was measured")
{
    test::FakeRenderer renderer ([] (const std::vector<float>& input) { return scaled (input, 0.5f); });

    const auto results = sonolint::runScenarios (scenarios ("  - id: gain\n"
                                                            "    signal: { type: pink_noise, level_dbfs: -18, duration_s: 1 }\n"
                                                            "    metrics: [level]\n"
                                                            "    expect: { level.gain_db: { approx: 0.0, tol: 0.05 } }\n"),
                                                 kPlugin, renderer);

    const auto& scenario = results["scenarios"][0];
    CHECK (scenario["status"] == "assertion_failed");
    CHECK (assertionOn (scenario, "level.gain_db")["passed"] == false);
    CHECK_THAT (assertionOn (scenario, "level.gain_db")["actual"].get<double>(), WithinAbs (-6.0206, 1.0e-3));
    CHECK (sonolint::hasFailures (results));

    CHECK (sonolint::formatSummary (results) == "FAIL   gain: level.gain_db is -6.0206, expected 0 within 0.05\n"
                                                "1 scenarios: 0 pass, 1 assertion_failed, 0 error\n");
}

TEST_CASE ("runner: a NaN in the output is an error, and the other scenarios still run")
{
    test::LambdaRenderer renderer ([] (const sonolint::RenderRequest& request)
    {
        sonolint::RenderResult result;
        result.output = request.input;

        if (std::get<double> (request.params.at (0).value) > 0.9)
            result.output.channels[0][100] = std::numeric_limits<float>::quiet_NaN();

        return result;
    });

    const auto results = sonolint::runScenarios (scenarios ("  - id: broken\n"
                                                            "    params: { Gain: 1.0 }\n"
                                                            "    signal: { type: silence, duration_s: 1 }\n"
                                                            "    metrics: [silence]\n"
                                                            "  - id: fine\n"
                                                            "    params: { Gain: 0.5 }\n"
                                                            "    signal: { type: silence, duration_s: 1 }\n"
                                                            "    metrics: [silence]\n"),
                                                 kPlugin, renderer);

    CHECK (results["scenarios"][0]["status"] == "error");
    CHECK (results["scenarios"][0]["error"] == "the output has NaN or infinite samples");
    CHECK (results["scenarios"][1]["status"] == "pass");
    CHECK (results["summary"] == json { { "pass", 1 }, { "assertion_failed", 0 }, { "error", 1 } });
    CHECK (sonolint::formatSummary (results) == "ERROR  broken: the output has NaN or infinite samples\n"
                                                "2 scenarios: 1 pass, 0 assertion_failed, 1 error\n");
}

TEST_CASE ("runner: the reported latency is taken off before measuring, and checked against the real one")
{
    const auto yaml = "  - id: delayed\n"
                      "    signal: { type: pink_noise, level_dbfs: -18, duration_s: 1 }\n"
                      "    metrics: [level, latency]\n"
                      "    expect: { level.gain_db: { approx: 0.0, tol: 0.01 } }\n";

    // Delays by 37 samples and says so: the aligned output is the input again.
    test::FakeRenderer honest (test::delay (37), 37);
    const auto good = sonolint::runScenarios (scenarios (yaml), kPlugin, honest)["scenarios"][0];

    CHECK (good["status"] == "pass");
    CHECK (good["reported_latency_samples"] == 37);
    CHECK_THAT (good["metrics"]["latency"]["mismatch_samples"].get<double>(), WithinAbs (0.0, 0.05));
    CHECK (assertionOn (good, "latency.mismatch_samples")["default"] == true);

    // Delays by 37 samples and reports 47: the default rule on the mismatch catches it.
    test::FakeRenderer lying (test::delay (37), 47);
    const auto bad = sonolint::runScenarios (scenarios (yaml), kPlugin, lying)["scenarios"][0];

    CHECK (bad["status"] == "assertion_failed");
    CHECK (assertionOn (bad, "latency.mismatch_samples")["passed"] == false);
    CHECK_THAT (bad["metrics"]["latency"]["mismatch_samples"].get<double>(), WithinAbs (-10.0, 0.05));
}

TEST_CASE ("runner: determinism is detected only in the renderer that adds random noise")
{
    const auto yaml = "  - id: twice\n"
                      "    signal: { type: pink_noise, level_dbfs: -18, duration_s: 1 }\n"
                      "    metrics: [determinism]\n";

    test::FakeRenderer deterministic ([] (const std::vector<float>& input) { return scaled (input, 0.5f); });
    const auto same = sonolint::runScenarios (scenarios (yaml), kPlugin, deterministic)["scenarios"][0];

    CHECK (same["status"] == "pass");
    CHECK (same["metrics"]["determinism"]["bit_identical"] == true);

    test::FakeRenderer noisy ([] (const std::vector<float>& input)
    {
        static std::mt19937 random (std::random_device {}());
        auto output = input;

        for (auto& sample : output)
            sample += 1.0e-5f * ((float) random() / (float) std::mt19937::max() - 0.5f);

        return output;
    });

    const auto different = sonolint::runScenarios (scenarios (yaml), kPlugin, noisy)["scenarios"][0];

    CHECK (different["status"] == "assertion_failed");
    CHECK (different["metrics"]["determinism"]["bit_identical"] == false);
    CHECK (different["metrics"]["determinism"]["max_diff_dbfs"].get<double>() > -120.0);
    CHECK (assertionOn (different, "determinism.bit_identical")["default"] == true);
}

TEST_CASE ("runner: block invariance compares constant blocks with random ones")
{
    // A plugin whose gain depends on how the host cuts the audio.
    test::LambdaRenderer blockBug ([] (const sonolint::RenderRequest& request)
    {
        sonolint::RenderResult result;
        result.output = request.input;

        if (request.blocks.randomSeed)
            for (auto& channel : result.output.channels)
                channel = scaled (channel, 0.999f);

        return result;
    });

    const auto results = sonolint::runScenarios (scenarios ("  - id: blocks\n"
                                                            "    signal: { type: pink_noise, level_dbfs: -18, duration_s: 1 }\n"
                                                            "    metrics: [block_invariance, determinism]\n"),
                                                 kPlugin, blockBug);

    const auto& scenario = results["scenarios"][0];
    CHECK (scenario["status"] == "assertion_failed");
    CHECK (assertionOn (scenario, "block_invariance.max_diff_dbfs")["passed"] == false);
    CHECK (assertionOn (scenario, "determinism.bit_identical")["passed"] == true);
    CHECK (scenario["metrics"]["block_invariance"]["max_diff_dbfs"].get<double>() > -120.0);
}

TEST_CASE ("runner: cpu comes from the time the renderer spent in each block")
{
    test::LambdaRenderer renderer ([] (const sonolint::RenderRequest& request)
    {
        sonolint::RenderResult result;
        result.output = request.input;
        result.processSecondsPerBlock.assign (100, 0.0001);   // 10 ms for 1 s of audio
        return result;
    });

    const auto results = sonolint::runScenarios (scenarios ("  - id: speed\n"
                                                            "    tail_ms: 0\n"
                                                            "    signal: { type: pink_noise, level_dbfs: -18, duration_s: 1 }\n"
                                                            "    metrics: [cpu]\n"),
                                                 kPlugin, renderer);

    const auto& cpu = results["scenarios"][0]["metrics"]["cpu"];
    CHECK_THAT (cpu["realtime_factor"].get<double>(), WithinAbs (0.01, 1.0e-9));
    CHECK_THAT (cpu["core_percent"].get<double>(), WithinAbs (1.0, 1.0e-7));
    CHECK_THAT (cpu["p95_block_us"].get<double>(), WithinAbs (100.0, 1.0e-6));
}

TEST_CASE ("runner: a sine moves to an FFT bin when harmonics are measured, and reports where it went")
{
    test::FakeRenderer renderer ([] (const std::vector<float>& input) { return input; });

    const auto results = sonolint::runScenarios (scenarios ("  - id: thd\n"
                                                            "    signal: { type: sine, freq_hz: 9973, level_dbfs: -6 }\n"
                                                            "    metrics: [harmonics, aliasing]\n"
                                                            "    expect: { harmonics.thdn_db: { max: -100 } }\n"
                                                            "  - id: level-only\n"
                                                            "    signal: { type: sine, freq_hz: 9973, level_dbfs: -6 }\n"
                                                            "    metrics: [level]\n"),
                                                 kPlugin, renderer);

    const auto& harmonics = results["scenarios"][0]["metrics"]["harmonics"];
    CHECK (results["scenarios"][0]["status"] == "pass");
    CHECK_THAT (harmonics["f0_hz"].get<double>(), WithinAbs (sonolint::snapToBin (9973.0, 48000.0, 65536), 1.0e-9));
    CHECK (harmonics["h_dbc"][1].is_null());   // the third harmonic is out of band
    CHECK (results["scenarios"][0]["metrics"]["aliasing"]["aliasing_dbc"].get<double>() < -120.0);

    // Without a spectral metric the sine stays where the config put it.
    CHECK_THAT (sonolint::makeSignal (scenarios ("  - id: x\n"
                                                 "    signal: { type: sine, freq_hz: 9973, level_dbfs: -6 }\n"
                                                 "    metrics: [level]\n")[0]
                                          .signal,
                                      48000.0, 2, 100.0, false)
                    .segments[0]
                    .freqHz,
                WithinAbs (9973.0, 1.0e-12));
}

TEST_CASE ("runner: a parameter the plugin doesn't have stops the run before rendering, with a suggestion")
{
    auto rendered = false;
    test::LambdaRenderer renderer ([&] (const sonolint::RenderRequest& request)
    {
        rendered = true;
        sonolint::RenderResult result;
        result.output = request.input;
        return result;
    });

    try
    {
        sonolint::runScenarios (scenarios ("  - id: typo\n"
                                           "    params: { Gian: 0.5 }\n"
                                           "    signal: { type: silence, duration_s: 1 }\n"
                                           "    metrics: [silence]\n"),
                                kPlugin, renderer);
        FAIL ("an unknown parameter must be rejected");
    }
    catch (const sonolint::Error& e)
    {
        CHECK (e.code == sonolint::ErrorCode::config);
        CHECK (std::string (e.what()) == "scenario \"typo\": unknown parameter \"Gian\"; did you mean \"Gain\"?");
    }

    CHECK_FALSE (rendered);

    // A parameter can also be named by its hosted ID.
    CHECK (sonolint::findParam (kPlugin, "Mix") == 1);
    CHECK (sonolint::findParam (kPlugin, "101") == 1);
}

TEST_CASE ("runner: a scenario that runs past its timeout hands over the results so far")
{
    test::LambdaRenderer slow ([] (const sonolint::RenderRequest& request)
    {
        if (std::get<double> (request.params.at (0).value) > 0.9)
            std::this_thread::sleep_for (std::chrono::milliseconds (400));

        sonolint::RenderResult result;
        result.output = request.input;
        return result;
    });

    std::atomic<bool> called { false };
    json partial;

    sonolint::RunOptions options;
    options.onTimeout = [&] (const json& results)
    {
        partial = results;
        called = true;   // the real one writes results.json and ends the process here
    };

    sonolint::runScenarios (scenarios ("  - id: quick\n"
                                       "    params: { Gain: 0.5 }\n"
                                       "    signal: { type: silence, duration_s: 0.5 }\n"
                                       "    metrics: [silence]\n"
                                       "  - id: stuck\n"
                                       "    timeout_s: 0.1\n"
                                       "    params: { Gain: 1.0 }\n"
                                       "    signal: { type: silence, duration_s: 0.5 }\n"
                                       "    metrics: [silence]\n"),
                            kPlugin, slow, options);

    REQUIRE (called);
    REQUIRE (partial["scenarios"].size() == 2);
    CHECK (partial["scenarios"][0]["status"] == "pass");
    CHECK (partial["scenarios"][1]["id"] == "stuck");
    CHECK (partial["scenarios"][1]["status"] == "error");
    CHECK (partial["scenarios"][1]["error"] == "timed out after 0.1 s");
    CHECK (partial["summary"]["error"] == 1);
}

TEST_CASE ("runner: a sine whose harmonics all fold onto other content gets a warning")
{
    test::FakeRenderer renderer ([] (const std::vector<float>& input) { return input; });

    // At 12 kHz and 48 kHz everything folds onto 0, 12 and 24 kHz.
    const auto results = sonolint::runScenarios (scenarios ("  - id: bad-frequency\n"
                                                            "    signal: { type: sine, freq_hz: 12000, level_dbfs: -6 }\n"
                                                            "    metrics: [aliasing]\n"
                                                            "  - id: good-frequency\n"
                                                            "    signal: { type: sine, freq_hz: 9973, level_dbfs: -6 }\n"
                                                            "    metrics: [aliasing]\n"),
                                                 kPlugin, renderer);

    REQUIRE (results["scenarios"][0].contains ("warnings"));
    CHECK (results["scenarios"][0]["warnings"].size() == 1);
    CHECK (results["scenarios"][0]["status"] == "pass");
    CHECK_FALSE (results["scenarios"][1].contains ("warnings"));
    CHECK (sonolint::formatSummary (results).rfind ("WARN   bad-frequency: more than half of the folded harmonics", 0) == 0);
}
