#include "core/config/config.h"
#include "core/config/text_match.h"
#include "core/error.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
// The message of the configuration error that parsing this YAML gives.
std::string errorOf (const std::string& yaml)
{
    try
    {
        sonolint::parseConfig (yaml, "/repo");
    }
    catch (const sonolint::Error& e)
    {
        CHECK (e.code == sonolint::ErrorCode::config);
        return e.what();
    }

    return "(no error)";
}

std::vector<std::string> idsOf (const sonolint::Config& config)
{
    std::vector<std::string> ids;

    for (const auto& scenario : config.scenarios)
        ids.push_back (scenario.id);

    return ids;
}

// One scenario with a noise signal; `extra` goes inside it and `top` at the top level.
std::string configWith (const std::string& extra, const std::string& top = "")
{
    return "version: 1\n" + top
         + "scenarios:\n"
           "  - id: a\n"
           "    signal: { type: pink_noise, level_dbfs: -18, duration_s: 1 }\n"
           "    metrics: [level]\n"
         + extra;
}
} // namespace

TEST_CASE ("config: the example of the specification parses into six scenarios")
{
    const auto config = sonolint::parseConfig (R"(
version: 1

plugin:
  path: build/Heft_artefacts/Release/VST3/Heft.vst3

defaults:
  sample_rate: 48000
  block_size: 512
  channels: 2
  warmup_ms: 200
  tail_ms: 100
  non_realtime: false
  analysis: { skip_ms: 100 }

tolerances:
  level.gain_db: { abs: 0.05 }
  harmonics.thd_db: { abs: 1.0 }
  cpu.*: { report_only: true }

scenarios:
  - id: transparent-1k
    description: Fatness 0 must not change the sound
    params:
      Fatness: 0.0
      Color: 0.5
      Output: { text: "0 dB" }
    signal: { type: sine, freq_hz: 1000, level_dbfs: -6, duration_s: 2 }
    metrics: [level, harmonics]
    expect:
      level.gain_db: { approx: 0.0, tol: 0.05 }
      harmonics.thdn_db: { max: -100 }

  - id: fat50-response
    params: { Fatness: 0.5 }
    signal: { type: stepped_sine, level_dbfs: -24, points: third_octave }
    metrics: [frequency_response]
    matrix:
      sample_rate: [44100, 48000, 96000]

  - id: fat100-hf
    params: { Fatness: 1.0, Color: 1.0 }
    signal: { type: sine, freq_hz: 9973, level_dbfs: -6, duration_s: 2 }
    metrics: [harmonics, aliasing, true_peak]

  - id: robustness
    params: { Fatness: 0.7 }
    signal: { type: pink_noise, level_dbfs: -18, duration_s: 3, seed: 1 }
    metrics: [determinism, block_invariance, latency, cpu]
)", "/repo");

    CHECK (config.pluginPath == std::filesystem::path ("/repo/build/Heft_artefacts/Release/VST3/Heft.vst3"));
    CHECK (idsOf (config) == std::vector<std::string> { "transparent-1k", "fat50-response@sr=44100", "fat50-response@sr=48000",
                                                        "fat50-response@sr=96000", "fat100-hf", "robustness" });

    CHECK (config.tolerances.at ("level.gain_db").abs == 0.05);
    CHECK (config.tolerances.at ("cpu.*").reportOnly);

    const auto& transparent = config.scenarios[0];
    CHECK (transparent.description == "Fatness 0 must not change the sound");
    CHECK (transparent.sampleRate == 48000.0);
    CHECK (transparent.blockSize == 512);
    CHECK (transparent.channels == 2);
    CHECK (transparent.timeoutS == 60.0);
    CHECK (transparent.metrics == std::vector<std::string> { "level", "harmonics" });

    REQUIRE (transparent.params.size() == 3);
    CHECK (transparent.params[0].key == "Fatness");
    CHECK (std::get<double> (transparent.params[0].value) == 0.0);
    CHECK (transparent.params[2].key == "Output");
    CHECK (std::get<std::string> (transparent.params[2].value) == "0 dB");

    CHECK (transparent.signal.type == sonolint::SignalType::sine);
    CHECK (transparent.signal.freqHz == 1000.0);
    CHECK (transparent.signal.levelDbfs == -6.0);
    CHECK (transparent.signal.snap);

    const auto& gain = transparent.expect.at ("level.gain_db");
    CHECK (gain.kind == sonolint::Expectation::Kind::approx);
    CHECK (gain.value == 0.0);
    CHECK (gain.other == 0.05);
    CHECK (transparent.expect.at ("harmonics.thdn_db").kind == sonolint::Expectation::Kind::max);

    CHECK (config.scenarios[1].sampleRate == 44100.0);
    CHECK (config.scenarios[3].sampleRate == 96000.0);
    CHECK (config.scenarios[1].signal.points.size() == 31);

    CHECK (config.scenarios[5].signal.type == sonolint::SignalType::pinkNoise);
    CHECK (config.scenarios[5].signal.durationS == 3.0);
}

TEST_CASE ("config: a matrix of 2 x 3 values gives 6 stable ids, keys in alphabetical order")
{
    const auto config = sonolint::parseConfig (configWith ("    matrix:\n"
                                                           "      sample_rate: [44100, 48000, 96000]\n"
                                                           "      block_size: [64, 512]\n"),
                                               "/repo");

    CHECK (idsOf (config) == std::vector<std::string> { "a@bs=64,sr=44100", "a@bs=64,sr=48000", "a@bs=64,sr=96000",
                                                        "a@bs=512,sr=44100", "a@bs=512,sr=48000", "a@bs=512,sr=96000" });
    CHECK (config.scenarios[0].blockSize == 64);
    CHECK (config.scenarios[0].sampleRate == 44100.0);
    CHECK (config.scenarios[5].blockSize == 512);
    CHECK (config.scenarios[5].sampleRate == 96000.0);
}

TEST_CASE ("config: a matrix can vary a parameter and the other host settings")
{
    const auto config = sonolint::parseConfig (configWith ("    params: { Gain: 0.2, Mix: 1 }\n"
                                                           "    matrix:\n"
                                                           "      params.Gain: [0, 0.5]\n"
                                                           "      non_realtime: [false, true]\n"
                                                           "      channels: [1]\n"),
                                               "/repo");

    CHECK (idsOf (config) == std::vector<std::string> { "a@ch=1,nrt=false,Gain=0", "a@ch=1,nrt=false,Gain=0.5",
                                                        "a@ch=1,nrt=true,Gain=0", "a@ch=1,nrt=true,Gain=0.5" });

    const auto& last = config.scenarios[3];
    CHECK (last.channels == 1);
    CHECK (last.nonRealtime);
    REQUIRE (last.params.size() == 2);   // Gain was replaced, not added again
    CHECK (std::get<double> (last.params[0].value) == 0.5);
}

TEST_CASE ("config: every mistake is reported with the exact place")
{
    struct Case
    {
        std::string yaml, expected;
    };

    const std::vector<Case> cases {
        { "scenarios: []\n", "version: missing: add \"version: 1\"" },
        { "version: 2\nscenarios: []\n", "version: this Sonolint reads version 1, got \"2\"" },
        { configWith ("", "scenarioz: 1\n"), "scenarioz: unknown key; did you mean \"scenarios\"?" },
        { "version: 1\n", "scenarios: missing: a config needs at least one scenario" },
        { configWith ("", "defaults: { channels: 3 }\n"), "defaults.channels: expected 1 or 2, got \"3\"" },
        { configWith ("", "defaults: { sample_rate: fast }\n"), "defaults.sample_rate: expected number, got \"fast\"" },
        { configWith ("", "defaults: { block_size: 0 }\n"), "defaults.block_size: expected 1 or more, got \"0\"" },
        { configWith ("", "tolerances: { harmonics.thd_bd: { abs: 1 } }\n"),
          "tolerances.harmonics.thd_bd: matches no metric; did you mean \"harmonics.thd_db\"?" },
        { configWith ("", "plugin: { file: x.vst3 }\n"), "plugin.file: unknown key" },

        { configWith ("    metrcs: [level]\n"), "scenarios[0].metrcs: unknown key; did you mean \"metrics\"?" },
        { configWith ("  - id: Bad_Id\n    signal: { type: silence, duration_s: 1 }\n    metrics: [silence]\n"),
          "scenarios[1].id: expected lowercase letters, digits and hyphens, starting with a letter or a digit, got \"Bad_Id\"" },
        { configWith ("  - id: a\n    signal: { type: silence, duration_s: 1 }\n    metrics: [silence]\n"),
          "scenarios[1].id: the id \"a\" is used twice" },
        { configWith ("    params: { Gain: 1.5 }\n"),
          "scenarios[0].params.Gain: expected a normalized value from 0 to 1, got \"1.5\"; for a value in the plugin's own units use "
          "{ text: \"...\" }" },
        { configWith ("    expect: { level.gain_bd: { max: 0 } }\n"),
          "scenarios[0].expect.level.gain_bd: unknown metric; did you mean \"level.gain_db\"?" },
        { configWith ("    expect: { harmonics.thd_db: { max: -60 } }\n"),
          "scenarios[0].expect.harmonics.thd_db: this scenario doesn't measure harmonics; add it to metrics" },
        { configWith ("    expect: { level.gain_db: { min: -1, max: 1 } }\n"),
          "scenarios[0].expect.level.gain_db: expected exactly one of approx (with tol), min, max, between or equals" },
        { configWith ("    expect: { level.gain_db: { approx: 0 } }\n"), "scenarios[0].expect.level.gain_db.tol: missing: approx needs a tolerance" },
        { configWith ("    matrix: { tail_ms: [1, 2] }\n"),
          "scenarios[0].matrix.tail_ms: expected sample_rate, block_size, channels, non_realtime or params.<name>" },
        { configWith ("    matrix: { sample_rate: [1, 2, 3, 4, 5, 6, 7, 8, 9], block_size: [1, 2, 3, 4, 5, 6, 7, 8] }\n"),
          "scenarios[0].matrix: 72 combinations; a matrix can have from 1 to 64" },

        { "version: 1\nscenarios:\n  - id: a\n    metrics: [level]\n", "scenarios[0].signal: missing: what should go into the plugin?" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: silence, duration_s: 1 }\n",
          "scenarios[0].metrics: missing: what should be measured?" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: sine, freq_hz: 1k, level_dbfs: -6 }\n    metrics: [level]\n",
          "scenarios[0].signal.freq_hz: expected number, got \"1k\"" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: sine, level_dbfs: -6 }\n    metrics: [level]\n",
          "scenarios[0].signal.freq_hz: missing: a sine signal needs it" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: sinus, freq_hz: 1000, level_dbfs: -6 }\n    metrics: [level]\n",
          "scenarios[0].signal.type: unknown signal type \"sinus\"; did you mean \"sine\"?" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: silence, duration_s: 1, level_dbfs: -6 }\n    metrics: [silence]\n",
          "scenarios[0].signal.level_dbfs: unknown key" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: silence, duration_s: 1 }\n    metrics: [silence, harmonic]\n",
          "scenarios[0].metrics[1]: unknown metric group \"harmonic\"; did you mean \"harmonics\"?" },
        { configWith ("  - id: b\n    signal: { type: pink_noise, level_dbfs: -18, duration_s: 1 }\n    metrics: [harmonics]\n"),
          "scenarios[1].signal.type: harmonics needs a sine, not pink_noise" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: sine, freq_hz: 1000, level_dbfs: -6 }\n    metrics: [latency]\n",
          "scenarios[0].signal.type: latency needs an impulse or noise (a periodic signal has no single delay), not sine" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: sine, freq_hz: 1000, level_dbfs: -6, duration_s: 0.3 }\n    metrics: [harmonics]\n",
          "scenarios[0].signal.duration_s: too short for harmonics or aliasing at 48000 Hz: it needs more than 0.446333 s" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: sine, freq_hz: 23000, level_dbfs: -6 }\n    metrics: [level]\n"
          "    matrix: { sample_rate: [48000, 44100] }\n",
          "scenarios[0].signal.freq_hz: must be below half the sample rate (44100 Hz)" },
        { "version: 1\nscenarios:\n  - id: a\n    signal: { type: impulse, position_ms: 1500 }\n    metrics: [latency]\n",
          "scenarios[0].signal.position_ms: the impulse must fall inside the signal, which lasts 1000 ms" },
    };

    for (const auto& test : cases)
    {
        CAPTURE (test.yaml);
        CHECK (errorOf (test.yaml) == test.expected);
    }

    CHECK (errorOf ("version: 1\nscenarios: [oops\n").rfind ("line 3, column 1: not valid YAML", 0) == 0);
}

TEST_CASE ("config: a signal can be written on one line for the command line")
{
    const auto sine = sonolint::parseSignalText ("sine:freq_hz=1000,level_dbfs=-6");
    CHECK (sine.type == sonolint::SignalType::sine);
    CHECK (sine.freqHz == 1000.0);
    CHECK (sine.levelDbfs == -6.0);
    CHECK (sine.durationS == 2.0);

    CHECK (sonolint::parseSignalText ("impulse").type == sonolint::SignalType::impulse);

    try
    {
        sonolint::parseSignalText ("sine:freq_hz=1000");
        FAIL ("a sine without a level must be rejected");
    }
    catch (const sonolint::Error& e)
    {
        CHECK (std::string (e.what()) == "--signal.level_dbfs: missing: a sine signal needs it");
    }
}

TEST_CASE ("config: --filter keeps the scenarios whose id matches a glob")
{
    const auto config = sonolint::parseConfig (configWith ("    matrix: { sample_rate: [44100, 48000] }\n"
                                                           "  - id: other\n"
                                                           "    signal: { type: silence, duration_s: 1 }\n"
                                                           "    metrics: [silence]\n"),
                                               "/repo");

    const auto ids = [&] (const std::vector<std::string>& globs)
    {
        sonolint::Config filtered;
        filtered.scenarios = sonolint::filterScenarios (config.scenarios, globs);
        return idsOf (filtered);
    };

    CHECK (ids ({}).size() == 3);
    CHECK (ids ({ "a@*" }) == std::vector<std::string> { "a@sr=44100", "a@sr=48000" });
    CHECK (ids ({ "*48000", "other" }) == std::vector<std::string> { "a@sr=48000", "other" });
    CHECK (ids ({ "nothing" }).empty());
}

TEST_CASE ("text: globs and typo suggestions")
{
    CHECK (sonolint::globMatch ("cpu.*", "cpu.core_percent"));
    CHECK (sonolint::globMatch ("*", ""));
    CHECK (sonolint::globMatch ("a*b*c", "a-b-b-c"));
    CHECK (sonolint::globMatch ("fat?0-1k", "fat50-1k"));
    CHECK_FALSE (sonolint::globMatch ("cpu.*", "level.gain_db"));
    CHECK_FALSE (sonolint::globMatch ("a*b", "a-b-c"));
    CHECK_FALSE (sonolint::globMatch ("abc", "ab"));

    CHECK (sonolint::closestMatch ("Fatnes", { "Color", "Fatness", "Mix" }) == "Fatness");
    CHECK (sonolint::closestMatch ("Threshold", { "Color", "Fatness", "Mix" }).empty());
    CHECK (sonolint::didYouMean ("Fatnes", { "Fatness" }) == "; did you mean \"Fatness\"?");
}
