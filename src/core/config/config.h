#pragma once

#include "core/runner/renderer.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sonolint
{
enum class SignalType { sine, steppedSine, impulse, whiteNoise, pinkNoise, silence, file };

// The `signal` of a scenario. Only the fields of its type are used.
struct SignalSpec
{
    SignalType type = SignalType::sine;
    std::string typeName;   // as written in the config: "stepped_sine"

    double freqHz = 1000.0;
    double levelDbfs = 0.0;
    double durationS = 2.0;
    double phaseDeg = 0.0;
    bool snap = true;

    std::vector<double> points;   // stepped_sine
    std::optional<double> stepS;

    double amplitude = 0.5;       // impulse
    double positionMs = 50.0;

    uint64_t seed = 1;            // noise
    std::filesystem::path path;   // file
};

// One rule of `expect`: an absolute check on a metric, whatever the baseline says.
struct Expectation
{
    enum class Kind { approx, min, max, between, equals };

    Kind kind = Kind::approx;
    double value = 0.0;           // approx: the target; min, max, equals: the number; between: the low end
    double other = 0.0;           // approx: the tolerance; between: the high end
    std::optional<bool> flag;     // equals: true or false, for metrics that are yes/no
    bool isDefault = false;       // added by Sonolint, not written in the config
};

// One entry of `tolerances`: how far a metric may move from the baseline.
struct Tolerance
{
    std::optional<double> abs, rel;
    bool reportOnly = false;
};

// A scenario ready to run: defaults applied and its matrix expanded.
struct Scenario
{
    std::string id;   // with the matrix suffix, e.g. "fat50-response@sr=96000"
    std::string description;

    double sampleRate = 48000.0;
    int blockSize = 512;
    int channels = 2;
    double warmupMs = 200.0;
    double tailMs = 100.0;
    bool nonRealtime = false;
    double skipMs = 100.0;
    double timeoutS = 60.0;

    std::vector<ParamValue> params;
    std::optional<std::filesystem::path> state;
    SignalSpec signal;
    std::vector<std::string> metrics;                 // group names, in the order written
    std::map<std::string, Expectation> expect;        // by metric path: "level.gain_db"
    std::map<std::string, Tolerance> tolerances;      // by metric path or glob; overrides the global ones
};

struct Config
{
    std::filesystem::path pluginPath;   // empty when the config has no plugin.path
    std::string pluginName;             // which plugin to load when the file holds several
    std::map<std::string, Tolerance> tolerances;
    std::vector<Scenario> scenarios;
};

// Reads sonolint.yml. The parser is strict: an unknown key is an error, to catch
// typos. Every problem throws Error (config) with the exact place, like
// `scenarios[3].signal.freq_hz: expected number, got "1k"`.
// Paths in the file are relative to baseDir, the folder of the config file.
Config parseConfig (const std::string& yaml, const std::filesystem::path& baseDir);
Config loadConfig (const std::filesystem::path& file);

// A signal written on the command line: "sine:freq_hz=1000,level_dbfs=-6".
SignalSpec parseSignalText (const std::string& text);

// The scenarios whose id matches at least one of the globs. No globs: all of them.
std::vector<Scenario> filterScenarios (const std::vector<Scenario>& scenarios, const std::vector<std::string>& globs);

// The fields of each metric group, e.g. "level" -> gain_db, peak_dbfs...
const std::map<std::string, std::vector<std::string>>& metricFields();
} // namespace sonolint
