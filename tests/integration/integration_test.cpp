// Runs the real `sonolint` binary against the test plugin and reads what it wrote.
// The paths come from CMake as compile definitions.
#include "core/io/wav.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

#ifndef _WIN32
 #include <sys/wait.h>
#endif

using Catch::Matchers::WithinAbs;
using json = nlohmann::json;

namespace
{
// In quotes and with the separators of this system, ready for a command line.
std::string quoted (std::filesystem::path path) { return "\"" + path.make_preferred().string() + "\""; }

std::filesystem::path fixture()
{
    return std::filesystem::path (SONOLINT_FIXTURES) / "SonolintFixture_artefacts" / SONOLINT_BUILD_CONFIG / "VST3" / "SonolintFixture.vst3";
}

// Runs `sonolint <arguments>` and returns its exit code.
int runSonolint (const std::string& arguments)
{
    auto command = quoted (SONOLINT_EXE) + " " + arguments;

   #ifdef _WIN32
    command = "\"" + command + "\"";   // cmd.exe strips one pair of quotes around the whole line
    return std::system (command.c_str());
   #else
    const auto status = std::system (command.c_str());
    return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
   #endif
}

struct Run
{
    int exitCode = -1;
    json results;

    const json& scenario (const std::string& id) const
    {
        for (const auto& entry : results.at ("scenarios"))
            if (entry.at ("id") == id)
                return entry;

        FAIL ("no scenario called " + id);
        return results;
    }

    // The metrics whose assertion failed in a scenario.
    std::vector<std::string> failed (const std::string& id) const
    {
        std::vector<std::string> metrics;

        for (const auto& assertion : scenario (id).at ("assertions"))
            if (! assertion.at ("passed").get<bool>())
                metrics.push_back (assertion.at ("metric").get<std::string>());

        return metrics;
    }
};

std::string configFile (const std::string& name) { return quoted (std::filesystem::path (SONOLINT_CONFIGS) / (name + ".yml")); }

// `sonolint <command>` with tests/integration/<config>.yml on the test plugin. `run`
// writes to its own folder, named after the config unless `out` says otherwise.
// Without arguments for the baseline, nothing is compared.
Run run (const std::string& config, const std::string& arguments = "--no-compare", const std::string& out = {})
{
    const auto folder = std::filesystem::path (SONOLINT_OUT) / (out.empty() ? config : out);
    std::filesystem::remove_all (folder);

    Run result;
    result.exitCode = runSonolint ("run --config " + configFile (config) + " --plugin " + quoted (fixture()) + " --out " + quoted (folder)
                                   + " " + arguments);

    if (std::ifstream file (folder / "results.json"); file)
        result.results = json::parse (file);

    return result;
}
} // namespace

TEST_CASE ("Gain: the gain is exact, with no distortion and no latency")
{
    const auto gain = run ("gain");

    CHECK (gain.exitCode == 0);
    CHECK (gain.results["plugin"]["name"] == "SonolintFixture");
    CHECK (gain.results["summary"]["pass"] == 4);

    // The expectations are in gain.yml; these are a second look at the numbers themselves.
    CHECK_THAT (gain.scenario ("minus-6-db")["metrics"]["level"]["gain_db"].get<double>(), WithinAbs (-6.0, 0.001));
    CHECK (gain.scenario ("plus-3-db-robust")["metrics"]["determinism"]["bit_identical"] == true);
    CHECK (gain.scenario ("plus-3-db-robust")["metrics"]["cpu"]["realtime_factor"].get<double>() > 0.0);
    CHECK (gain.scenario ("mono")["metrics"]["level"]["per_channel"].size() == 1);
}

TEST_CASE ("Delay: the real delay matches the reported one, and a lie is caught")
{
    const auto honest = run ("delay");

    CHECK (honest.exitCode == 0);
    CHECK (honest.scenario ("honest")["metrics"]["latency"]["reported_samples"] == 37);
    CHECK_THAT (honest.scenario ("honest")["metrics"]["latency"]["mismatch_samples"].get<double>(), WithinAbs (0.0, 0.05));
    CHECK (honest.scenario ("honest-noise")["status"] == "pass");

    const auto lie = run ("delay-lie");

    CHECK (lie.exitCode == 1);
    CHECK (lie.scenario ("lying")["status"] == "assertion_failed");
    CHECK (lie.failed ("lying") == std::vector<std::string> { "latency.mismatch_samples" });
    CHECK_THAT (lie.scenario ("lying")["metrics"]["latency"]["mismatch_samples"].get<double>(), WithinAbs (-10.0, 0.05));
}

TEST_CASE ("Clipper: odd harmonics only, and 4x oversampling takes more than 20 dB off the aliasing")
{
    const auto clipper = run ("clipper");

    CHECK (clipper.exitCode == 0);

    const auto& harmonics = clipper.scenario ("odd-harmonics")["metrics"]["harmonics"]["h_dbc"];
    CHECK (harmonics[1].get<double>() > -20.0);    // H3
    CHECK (harmonics[3].get<double>() > -40.0);    // H5
    CHECK (harmonics[0].get<double>() < -100.0);   // H2
    CHECK (harmonics[2].get<double>() < -100.0);   // H4

    const auto at1x = clipper.scenario ("aliasing-1x")["metrics"]["aliasing"]["aliasing_dbc"].get<double>();
    const auto at4x = clipper.scenario ("aliasing-4x")["metrics"]["aliasing"]["aliasing_dbc"].get<double>();
    CHECK (at1x > -30.0);
    CHECK (at1x - at4x > 20.0);
}

TEST_CASE ("Filter: the measured response is the formula of a first-order low-pass")
{
    const auto filter = run ("filter");

    CHECK (filter.exitCode == 0);

    const auto& response = filter.scenario ("response")["metrics"]["frequency_response"];
    REQUIRE (response["freqs_hz"].size() == 31);

    // H (z) = b (1 + z^-1) / (1 + a z^-1), with the coefficients of the Filter behaviour at 1 kHz and 48 kHz.
    const auto k = std::tan (std::numbers::pi * 1000.0 / 48000.0);
    const auto b = k / (1.0 + k), a = (k - 1.0) / (k + 1.0);

    for (size_t i = 0; i < response["freqs_hz"].size(); ++i)
    {
        const auto freq = response["freqs_hz"][i].get<double>();
        const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * freq / 48000.0);
        const auto expected = b * (1.0 + z1) / (1.0 + a * z1);

        CAPTURE (freq);
        CHECK_THAT (response["magnitude_db"][i].get<double>(), WithinAbs (20.0 * std::log10 (std::abs (expected)), 0.05));
        CHECK_THAT (response["phase_deg"][i].get<double>(), WithinAbs (std::arg (expected) * 180.0 / std::numbers::pi, 0.5));
    }
}

TEST_CASE ("Buggy: each bug fails its own check and no other")
{
    const auto buggy = run ("buggy");

    CHECK (buggy.exitCode == 1);
    CHECK (buggy.results["summary"]["pass"] == 3);
    CHECK (buggy.results["summary"]["assertion_failed"] == 3);
    CHECK (buggy.results["summary"]["error"] == 1);

    CHECK (buggy.scenario ("clean")["status"] == "pass");
    CHECK (buggy.scenario ("clean-silence")["status"] == "pass");
    CHECK (buggy.scenario ("nan-not-triggered")["status"] == "pass");

    CHECK (buggy.failed ("nondeterministic") == std::vector<std::string> { "determinism.bit_identical" });
    CHECK (buggy.failed ("block-bug") == std::vector<std::string> { "block_invariance.max_diff_dbfs" });
    CHECK (buggy.failed ("oscillate") == std::vector<std::string> { "silence.noise_floor_dbfs" });

    CHECK (buggy.scenario ("nan")["status"] == "error");
    CHECK (buggy.scenario ("nan")["error"] == "the output has NaN or infinite samples");
}

TEST_CASE ("sonolint render writes what came out of the plugin to a WAV file")
{
    const auto out = std::filesystem::path (SONOLINT_OUT) / "render.wav";
    std::filesystem::create_directories (out.parent_path());
    std::filesystem::remove (out);

    const auto exitCode = runSonolint ("render --plugin " + quoted (fixture()) + " --signal sine:freq_hz=1000,level_dbfs=-6"
                                    + " --param \"Gain=-6 dB\" --out " + quoted (out));
    REQUIRE (exitCode == 0);

    const auto audio = sonolint::readWav (out);
    CHECK (audio.sampleRate == 48000.0);
    REQUIRE (audio.channels.size() == 2);
    REQUIRE (audio.numSamples() == 96000 + 4800);   // 2 s plus the 100 ms tail

    // A sine at -6 dBFS through -6 dB of gain peaks at -12 dBFS: 0.2512.
    CHECK_THAT (audio.channels[0][12 + 4800], WithinAbs (0.2512, 0.0005));
}

TEST_CASE ("Baseline: without one every value is new, and after baseline update the same run passes")
{
    const auto baseline = std::filesystem::path (SONOLINT_OUT) / "baseline-flow" / "baseline.json";
    std::filesystem::remove_all (baseline.parent_path());
    const auto withBaseline = "--baseline " + quoted (baseline);

    const auto first = run ("gain", withBaseline, "gain-first");
    CHECK (first.exitCode == 0);
    CHECK (first.results["summary"]["new"] == 4);
    CHECK (first.results["baseline"]["file"].is_string());

    CHECK (runSonolint ("baseline update --config " + configFile ("gain") + " --plugin " + quoted (fixture()) + " " + withBaseline) == 0);

    std::ifstream file (baseline, std::ios::binary);
    REQUIRE (file);
    const auto stored = json::parse (file);
    CHECK (stored["schema"] == "sonolint.baseline/v1");
    CHECK (stored["plugin"]["name"] == "SonolintFixture");
    CHECK (stored["platforms"].size() == 1);
    CHECK (stored["platforms"].begin()->at ("scenarios").size() == 4);

    const auto second = run ("gain", withBaseline, "gain-second");
    CHECK (second.exitCode == 0);
    CHECK (second.results["summary"]["pass"] == 4);

    for (const auto& scenario : second.results["scenarios"])
        for (const auto& entry : scenario["comparison"])
        {
            CAPTURE (scenario["id"], entry["metric"]);
            CHECK (entry["status"] == "pass");
        }
}

TEST_CASE ("Baseline: 0.2 dB more gain fails with a tolerance of 0.05 dB and passes with 0.5 dB")
{
    const auto baseline = std::filesystem::path (SONOLINT_OUT) / "baseline-gain" / "baseline.json";
    std::filesystem::remove_all (baseline.parent_path());
    const auto withBaseline = "--baseline " + quoted (baseline);

    REQUIRE (runSonolint ("baseline update --config " + configFile ("gain-change-before") + " --plugin " + quoted (fixture()) + " "
                          + withBaseline) == 0);

    const auto tight = run ("gain-change-after", withBaseline + " --filter tight", "gain-tight");
    CHECK (tight.exitCode == 1);
    CHECK (tight.scenario ("tight")["status"] == "changed");

    for (const auto& entry : tight.scenario ("tight")["comparison"])
        if (entry["metric"] == "level.gain_db")
        {
            CHECK (entry["status"] == "changed");
            CHECK_THAT (entry["delta"].get<double>(), WithinAbs (0.2, 0.001));
        }

    const auto loose = run ("gain-change-after", withBaseline + " --filter loose", "gain-loose");
    CHECK (loose.exitCode == 0);
    CHECK (loose.scenario ("loose")["status"] == "pass");
    CHECK (loose.results["summary"]["removed"] == 0);   // tight is outside the filter, not gone

    // The same change, with other policies.
    CHECK (run ("gain-change-after", withBaseline + " --filter tight --fail-on assertions", "gain-assertions").exitCode == 0);
    CHECK (run ("gain-change-after", withBaseline + " --filter tight --fail-on none", "gain-none").exitCode == 0);
}

TEST_CASE ("Exit codes: a plugin that doesn't exist is 3 and a wrong config is 2")
{
    const auto out = quoted (std::filesystem::path (SONOLINT_OUT) / "exit-codes");

    CHECK (runSonolint ("run --config " + configFile ("gain") + " --plugin " + quoted (std::filesystem::path (SONOLINT_OUT) / "missing.vst3")
                        + " --no-compare --out " + out) == 3);
    CHECK (runSonolint ("run --config " + configFile ("invalid") + " --plugin " + quoted (fixture()) + " --no-compare --out " + out) == 2);
    CHECK (runSonolint ("run --config " + configFile ("gain") + " --plugin " + quoted (fixture()) + " --fail-on sometimes --out " + out) == 2);
}

TEST_CASE ("Reproducibility: two runs write the same results, apart from the machine and the times")
{
    const auto withoutTimes = [] (json results)
    {
        results.erase ("environment");

        for (auto& scenario : results["scenarios"])
        {
            scenario.erase ("seconds");
            scenario["metrics"].erase ("cpu");
        }

        return results;
    };

    const auto first = run ("gain", "--no-compare", "repeat-1");
    const auto second = run ("gain", "--no-compare", "repeat-2");

    REQUIRE (first.results.is_object());
    CHECK (withoutTimes (first.results) == withoutTimes (second.results));
}

TEST_CASE ("Reports: --format chooses the files, --save-audio writes the WAVs, and the footer names the commit")
{
    const auto folder = std::filesystem::path (SONOLINT_OUT) / "reports";

    // By default, the four reports.
    run ("gain", "--no-compare", "reports-default");
    for (const auto* file : { "results.json", "report.md", "report.html", "junit.xml" })
    {
        CAPTURE (file);
        CHECK (std::filesystem::exists (std::filesystem::path (SONOLINT_OUT) / "reports-default" / file));
    }

    // Only what --format asks for.
    std::filesystem::remove_all (folder);
    REQUIRE (runSonolint ("run --config " + configFile ("gain") + " --plugin " + quoted (fixture()) + " --no-compare --out " + quoted (folder)
                          + " --format md,junit --save-audio --artifact-url https://example.com/run/1 --commit 0123456789abcdef0123456789abcdef01234567"
                          + " --merged-into main") == 0);

    CHECK (std::filesystem::exists (folder / "report.md"));
    CHECK (std::filesystem::exists (folder / "junit.xml"));
    CHECK_FALSE (std::filesystem::exists (folder / "results.json"));
    CHECK_FALSE (std::filesystem::exists (folder / "report.html"));

    std::ifstream markdown (folder / "report.md", std::ios::binary);
    const std::string text { std::istreambuf_iterator<char> (markdown), std::istreambuf_iterator<char>() };
    CHECK (text.find ("[Full report](https://example.com/run/1)") != std::string::npos);
    CHECK (text.find ("Commit 0123456 (merged into main)") != std::string::npos);

    // The input and the raw output of each scenario: 2 s of sine and the 100 ms tail.
    const auto input = sonolint::readWav (folder / "audio" / "minus-6-db.input.wav");
    const auto output = sonolint::readWav (folder / "audio" / "minus-6-db.output.wav");
    CHECK (input.numSamples() == 96000);
    CHECK (output.numSamples() == 96000 + 4800);
    CHECK (output.channels.size() == 2);

    CHECK (runSonolint ("run --config " + configFile ("gain") + " --plugin " + quoted (fixture()) + " --format pdf --out " + quoted (folder)) == 2);
}
