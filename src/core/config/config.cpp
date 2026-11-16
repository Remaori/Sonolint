#include "core/config/config.h"

#include "core/config/text_match.h"
#include "core/error.h"
#include "core/io/path_text.h"
#include "core/signals/generators.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace sonolint
{
namespace
{
// A YAML node together with where it sits in the file, for error messages.
struct At
{
    YAML::Node node;
    std::string path;

    // Only call these on a map or a list: yaml-cpp throws on anything else.
    At child (const std::string& key) const
    {
        const YAML::Node& constNode = node;   // the const operator[] never adds the key
        return { constNode[key], path.empty() ? key : path + "." + key };
    }

    At item (size_t index) const
    {
        const YAML::Node& constNode = node;
        return { constNode[index], path + "[" + std::to_string (index) + "]" };
    }

    bool exists() const { return node.IsDefined() && ! node.IsNull(); }
};

[[noreturn]] void fail (const At& at, const std::string& message)
{
    throw Error (ErrorCode::config, (at.path.empty() ? "" : at.path + ": ") + message);
}

std::string describe (const YAML::Node& node)
{
    if (! node.IsDefined() || node.IsNull())
        return "nothing";

    return node.IsScalar() ? "\"" + node.Scalar() + "\"" : node.IsMap() ? "a map" : "a list";
}

template <typename Value>
Value scalar (const At& at, const std::string& expected)
{
    if (at.exists() && at.node.IsScalar())
    {
        try
        {
            return at.node.as<Value>();
        }
        catch (const YAML::Exception&)
        {
        }
    }

    fail (at, "expected " + expected + ", got " + describe (at.node));
}

double number (const At& at)
{
    const auto value = scalar<double> (at, "number");

    if (! std::isfinite (value))
        fail (at, "expected number, got " + describe (at.node));

    return value;
}

double above (const At& at, double limit)
{
    const auto value = number (at);

    if (value <= limit)
        fail (at, "expected a number above " + std::to_string ((long long) limit) + ", got " + describe (at.node));

    return value;
}

double atLeast (const At& at, double limit)
{
    const auto value = number (at);

    if (value < limit)
        fail (at, "expected " + std::to_string ((long long) limit) + " or more, got " + describe (at.node));

    return value;
}

int wholeNumber (const At& at, int smallest)
{
    const auto value = scalar<int> (at, "whole number");

    if (value < smallest)
        fail (at, "expected " + std::to_string (smallest) + " or more, got " + describe (at.node));

    return value;
}

bool boolean (const At& at) { return scalar<bool> (at, "true or false"); }
std::string text (const At& at) { return scalar<std::string> (at, "text"); }

void requireMap (const At& at)
{
    if (! at.exists() || ! at.node.IsMap())
        fail (at, "expected a map, got " + describe (at.node));
}

void requireList (const At& at)
{
    if (! at.exists() || ! at.node.IsSequence() || at.node.size() == 0)
        fail (at, "expected a list with at least one item, got " + describe (at.node));
}

// The keys of a map, in the order written. A key that repeats is an error.
std::vector<std::string> keysOf (const At& at)
{
    requireMap (at);
    std::vector<std::string> keys;

    for (const auto& entry : at.node)
    {
        if (! entry.first.IsScalar())
            fail (at, "expected plain text keys");

        const auto key = entry.first.Scalar();

        if (std::find (keys.begin(), keys.end(), key) != keys.end())
            fail (at.child (key), "this key appears twice");

        keys.push_back (key);
    }

    return keys;
}

// The node must be a map and may only have these keys.
void checkKeys (const At& at, const std::vector<std::string>& allowed)
{
    for (const auto& key : keysOf (at))
        if (std::find (allowed.begin(), allowed.end(), key) == allowed.end())
            fail (at.child (key), "unknown key" + didYouMean (key, allowed));
}

std::filesystem::path resolve (const std::filesystem::path& baseDir, const At& at)
{
    return (baseDir / fromUtf8 (text (at))).lexically_normal();
}

std::vector<std::string> allMetricPaths()
{
    std::vector<std::string> paths;

    for (const auto& [group, fields] : metricFields())
        for (const auto& field : fields)
            paths.push_back (group + "." + field);

    return paths;
}

std::vector<std::string> metricGroups()
{
    std::vector<std::string> groups;

    for (const auto& entry : metricFields())
        groups.push_back (entry.first);

    return groups;
}

//==============================================================================
const std::vector<std::string> kSettingKeys { "sample_rate", "block_size", "channels", "warmup_ms", "tail_ms",
                                              "non_realtime", "analysis", "timeout_s" };

// One host setting, from `defaults`, from a scenario or from a matrix.
void applySetting (Scenario& scenario, const std::string& key, const At& value)
{
    if (key == "sample_rate")
        scenario.sampleRate = above (value, 0.0);
    else if (key == "block_size")
        scenario.blockSize = wholeNumber (value, 1);
    else if (key == "channels")
    {
        scenario.channels = wholeNumber (value, 1);

        if (scenario.channels > 2)
            fail (value, "expected 1 or 2, got " + describe (value.node));
    }
    else if (key == "warmup_ms")
        scenario.warmupMs = atLeast (value, 0.0);
    else if (key == "tail_ms")
        scenario.tailMs = atLeast (value, 0.0);
    else if (key == "non_realtime")
        scenario.nonRealtime = boolean (value);
    else if (key == "timeout_s")
        scenario.timeoutS = above (value, 0.0);
    else if (key == "analysis")
    {
        checkKeys (value, { "skip_ms" });

        if (const auto skip = value.child ("skip_ms"); skip.exists())
            scenario.skipMs = atLeast (skip, 0.0);
    }
}

void applySettings (Scenario& scenario, const At& at)
{
    for (const auto& key : kSettingKeys)
        if (const auto value = at.child (key); value.exists())
            applySetting (scenario, key, value);
}

//==============================================================================
SignalSpec parseSignal (const At& at, const std::filesystem::path& baseDir)
{
    if (! at.exists())
        fail (at, "missing: what should go into the plugin?");

    requireMap (at);

    struct TypeInfo
    {
        SignalType type;
        std::vector<std::string> keys;
    };

    static const std::map<std::string, TypeInfo> types {
        { "sine", { SignalType::sine, { "type", "freq_hz", "level_dbfs", "duration_s", "phase_deg", "snap" } } },
        { "stepped_sine", { SignalType::steppedSine, { "type", "points", "level_dbfs", "step_s" } } },
        { "impulse", { SignalType::impulse, { "type", "amplitude", "position_ms", "duration_s" } } },
        { "white_noise", { SignalType::whiteNoise, { "type", "level_dbfs", "duration_s", "seed" } } },
        { "pink_noise", { SignalType::pinkNoise, { "type", "level_dbfs", "duration_s", "seed" } } },
        { "silence", { SignalType::silence, { "type", "duration_s" } } },
        { "file", { SignalType::file, { "type", "path" } } } };

    const auto typeAt = at.child ("type");

    if (! typeAt.exists())
        fail (typeAt, "missing");

    SignalSpec signal;
    signal.typeName = text (typeAt);
    const auto found = types.find (signal.typeName);

    if (found == types.end())
    {
        std::vector<std::string> names;

        for (const auto& entry : types)
            names.push_back (entry.first);

        fail (typeAt, "unknown signal type \"" + signal.typeName + "\"" + didYouMean (signal.typeName, names));
    }

    signal.type = found->second.type;
    checkKeys (at, found->second.keys);

    const auto has = [&] (const std::string& key) { return at.child (key).exists(); };
    const auto required = [&] (const std::string& key)
    {
        if (! has (key))
            fail (at.child (key), "missing: a " + signal.typeName + " signal needs it");

        return at.child (key);
    };

    switch (signal.type)
    {
        case SignalType::sine:
            signal.freqHz    = above (required ("freq_hz"), 0.0);
            signal.levelDbfs = number (required ("level_dbfs"));

            if (has ("duration_s"))  signal.durationS = above (at.child ("duration_s"), 0.0);
            if (has ("phase_deg"))   signal.phaseDeg  = number (at.child ("phase_deg"));
            if (has ("snap"))        signal.snap      = boolean (at.child ("snap"));
            break;

        case SignalType::steppedSine:
        {
            const auto points = required ("points");

            if (points.node.IsSequence())
            {
                requireList (points);

                for (size_t i = 0; i < points.node.size(); ++i)
                    signal.points.push_back (above (points.item (i), 0.0));
            }
            else if (points.node.IsScalar() && points.node.Scalar() == "third_octave")
                signal.points = thirdOctaveFrequencies();
            else if (points.node.IsScalar() && points.node.Scalar() == "octave")
                signal.points = octaveFrequencies();
            else
                fail (points, "expected third_octave, octave or a list of frequencies, got " + describe (points.node));

            signal.levelDbfs = number (required ("level_dbfs"));

            if (has ("step_s"))
                signal.stepS = above (at.child ("step_s"), 0.0);

            break;
        }

        case SignalType::impulse:
            signal.durationS = 1.0;

            if (has ("amplitude"))    signal.amplitude  = number (at.child ("amplitude"));
            if (has ("position_ms"))  signal.positionMs = atLeast (at.child ("position_ms"), 0.0);
            if (has ("duration_s"))   signal.durationS  = above (at.child ("duration_s"), 0.0);

            if (signal.positionMs >= 1000.0 * signal.durationS)
                fail (at.child ("position_ms"), "the impulse must fall inside the signal, which lasts "
                                                    + std::to_string ((long long) (1000.0 * signal.durationS)) + " ms");
            break;

        case SignalType::whiteNoise:
        case SignalType::pinkNoise:
            signal.levelDbfs = number (required ("level_dbfs"));
            signal.durationS = above (required ("duration_s"), 0.0);

            if (has ("seed"))
                signal.seed = scalar<uint64_t> (at.child ("seed"), "whole number of 0 or more");

            break;

        case SignalType::silence:
            signal.durationS = above (required ("duration_s"), 0.0);
            break;

        case SignalType::file:
            signal.path = resolve (baseDir, required ("path"));
            break;
    }

    return signal;
}

ParamValue parseParam (const std::string& key, const At& value)
{
    if (value.exists() && value.node.IsMap())
    {
        checkKeys (value, { "text" });

        if (! value.child ("text").exists())
            fail (value, "expected a number from 0 to 1 or { text: \"...\" }");

        return { key, text (value.child ("text")) };
    }

    const auto normalized = number (value);

    if (normalized < 0.0 || normalized > 1.0)
        fail (value, "expected a normalized value from 0 to 1, got " + describe (value.node)
                         + "; for a value in the plugin's own units use { text: \"...\" }");

    return { key, normalized };
}

// Sets a parameter, replacing an earlier value for the same key.
void setParam (std::vector<ParamValue>& params, ParamValue param)
{
    const auto existing = std::find_if (params.begin(), params.end(), [&] (const ParamValue& p) { return p.key == param.key; });

    if (existing != params.end())
        *existing = std::move (param);
    else
        params.push_back (std::move (param));
}

Expectation parseExpectation (const At& at)
{
    checkKeys (at, { "approx", "tol", "min", "max", "between", "equals" });

    Expectation rule;
    int rules = 0;

    if (const auto approx = at.child ("approx"); approx.exists())
    {
        if (! at.child ("tol").exists())
            fail (at.child ("tol"), "missing: approx needs a tolerance");

        rule.kind  = Expectation::Kind::approx;
        rule.value = number (approx);
        rule.other = atLeast (at.child ("tol"), 0.0);
        ++rules;
    }
    else if (at.child ("tol").exists())
    {
        fail (at.child ("tol"), "tol only goes with approx");
    }

    if (const auto least = at.child ("min"); least.exists())
    {
        rule.kind  = Expectation::Kind::min;
        rule.value = number (least);
        ++rules;
    }

    if (const auto most = at.child ("max"); most.exists())
    {
        rule.kind  = Expectation::Kind::max;
        rule.value = number (most);
        ++rules;
    }

    if (const auto between = at.child ("between"); between.exists())
    {
        if (! between.node.IsSequence() || between.node.size() != 2)
            fail (between, "expected a list of two numbers, got " + describe (between.node));

        rule.kind  = Expectation::Kind::between;
        rule.value = number (between.item (0));
        rule.other = number (between.item (1));

        if (rule.value > rule.other)
            fail (between, "the first number must not be above the second");

        ++rules;
    }

    if (const auto equals = at.child ("equals"); equals.exists())
    {
        rule.kind = Expectation::Kind::equals;

        if (equals.node.IsScalar() && (equals.node.Scalar() == "true" || equals.node.Scalar() == "false"))
            rule.flag = equals.node.Scalar() == "true";
        else
            rule.value = scalar<double> (equals, "a number, true or false");

        ++rules;
    }

    if (rules != 1)
        fail (at, "expected exactly one of approx (with tol), min, max, between or equals");

    return rule;
}

std::map<std::string, Expectation> parseExpect (const At& at, const std::vector<std::string>& metrics)
{
    std::map<std::string, Expectation> expect;

    if (! at.exists())
        return expect;

    for (const auto& path : keysOf (at))
    {
        const auto dot   = path.find ('.');
        const auto group = path.substr (0, dot);

        if (dot == std::string::npos || metricFields().count (group) == 0)
            fail (at.child (path), "unknown metric" + didYouMean (path, allMetricPaths()));

        if (std::find (metrics.begin(), metrics.end(), group) == metrics.end())
            fail (at.child (path), "this scenario doesn't measure " + group + "; add it to metrics");

        const auto& fields = metricFields().at (group);

        if (std::find (fields.begin(), fields.end(), path.substr (dot + 1)) == fields.end())
            fail (at.child (path), "unknown metric" + didYouMean (path, allMetricPaths()));

        expect[path] = parseExpectation (at.child (path));
    }

    return expect;
}

std::map<std::string, Tolerance> parseTolerances (const At& at)
{
    std::map<std::string, Tolerance> tolerances;

    if (! at.exists())
        return tolerances;

    const auto paths = allMetricPaths();

    for (const auto& pattern : keysOf (at))
    {
        const auto entry = at.child (pattern);

        if (std::none_of (paths.begin(), paths.end(), [&] (const std::string& path) { return globMatch (pattern, path); }))
            fail (entry, "matches no metric" + didYouMean (pattern, paths));

        checkKeys (entry, { "abs", "rel", "report_only" });
        Tolerance tolerance;

        if (const auto absolute = entry.child ("abs"); absolute.exists())  tolerance.abs = atLeast (absolute, 0.0);
        if (const auto relative = entry.child ("rel"); relative.exists())  tolerance.rel = atLeast (relative, 0.0);
        if (const auto only = entry.child ("report_only"); only.exists())  tolerance.reportOnly = boolean (only);

        tolerances[pattern] = tolerance;
    }

    return tolerances;
}

std::vector<std::string> parseMetrics (const At& at)
{
    if (! at.exists())
        fail (at, "missing: what should be measured?");

    requireList (at);
    std::vector<std::string> metrics;

    for (size_t i = 0; i < at.node.size(); ++i)
    {
        const auto name = text (at.item (i));

        if (metricFields().count (name) == 0)
            fail (at.item (i), "unknown metric group \"" + name + "\"" + didYouMean (name, metricGroups()));

        if (std::find (metrics.begin(), metrics.end(), name) != metrics.end())
            fail (at.item (i), "\"" + name + "\" is listed twice");

        metrics.push_back (name);
    }

    return metrics;
}

//==============================================================================
bool measures (const Scenario& scenario, const std::string& group)
{
    return std::find (scenario.metrics.begin(), scenario.metrics.end(), group) != scenario.metrics.end();
}

// Checks that need the final values of a scenario, after its matrix was applied.
void validate (const Scenario& scenario, const At& at)
{
    const auto& signal = scenario.signal;
    const auto signalAt = at.child ("signal");

    const auto needs = [&] (const std::string& group, std::initializer_list<SignalType> types, const std::string& what)
    {
        if (measures (scenario, group) && std::find (types.begin(), types.end(), signal.type) == types.end())
            fail (signalAt.child ("type"), group + " needs " + what + ", not " + signal.typeName);
    };

    needs ("harmonics", { SignalType::sine }, "a sine");
    needs ("aliasing", { SignalType::sine }, "a sine");
    needs ("frequency_response", { SignalType::steppedSine }, "a stepped_sine");
    needs ("latency", { SignalType::impulse, SignalType::whiteNoise, SignalType::pinkNoise },
           "an impulse or noise (a periodic signal has no single delay)");
    needs ("silence", { SignalType::silence }, "silence");

    if (signal.type == SignalType::sine && signal.freqHz >= 0.5 * scenario.sampleRate)
        fail (signalAt.child ("freq_hz"), "must be below half the sample rate (" + std::to_string ((long long) scenario.sampleRate) + " Hz)");

    if (measures (scenario, "harmonics") || measures (scenario, "aliasing"))
    {
        // The spectrum needs 2^14 samples after the skipped part and before the 5 ms fade-out.
        const auto needed = 16384.0 / scenario.sampleRate + scenario.skipMs / 1000.0 + 0.005;

        if (signal.durationS < needed + 1.0 / scenario.sampleRate)
        {
            std::ostringstream message;
            message << "too short for harmonics or aliasing at " << scenario.sampleRate << " Hz: it needs more than " << needed << " s";
            fail (signalAt.child ("duration_s"), message.str());
        }
    }
}

const std::map<std::string, std::string> kMatrixShortNames { { "block_size", "bs" }, { "channels", "ch" },
                                                             { "non_realtime", "nrt" }, { "sample_rate", "sr" } };

std::vector<Scenario> parseScenario (const At& at, const Scenario& defaults, const std::filesystem::path& baseDir)
{
    auto keys = kSettingKeys;
    keys.insert (keys.end(), { "id", "description", "params", "state", "signal", "metrics", "expect", "tolerances", "matrix" });
    checkKeys (at, keys);

    auto scenario = defaults;

    const auto idAt = at.child ("id");

    if (! idAt.exists())
        fail (idAt, "missing: every scenario needs an id");

    scenario.id = text (idAt);

    const auto idChar = [] (char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };

    if (scenario.id.empty() || ! idChar (scenario.id.front())
        || ! std::all_of (scenario.id.begin(), scenario.id.end(), [&] (char c) { return idChar (c) || c == '-'; }))
        fail (idAt, "expected lowercase letters, digits and hyphens, starting with a letter or a digit, got " + describe (idAt.node));

    if (const auto description = at.child ("description"); description.exists())
        scenario.description = text (description);

    applySettings (scenario, at);

    if (const auto params = at.child ("params"); params.exists())
        for (const auto& key : keysOf (params))
            setParam (scenario.params, parseParam (key, params.child (key)));

    if (const auto state = at.child ("state"); state.exists())
        scenario.state = resolve (baseDir, state);

    scenario.signal     = parseSignal (at.child ("signal"), baseDir);
    scenario.metrics    = parseMetrics (at.child ("metrics"));
    scenario.expect     = parseExpect (at.child ("expect"), scenario.metrics);
    scenario.tolerances = parseTolerances (at.child ("tolerances"));

    const auto matrixAt = at.child ("matrix");

    if (! matrixAt.exists())
    {
        validate (scenario, at);
        return { scenario };
    }

    // The matrix: every combination of the listed values, in the alphabetical order of the keys.
    std::vector<std::string> matrixKeys = keysOf (matrixAt);
    std::sort (matrixKeys.begin(), matrixKeys.end());
    size_t combinations = 1;

    for (const auto& key : matrixKeys)
    {
        if (kMatrixShortNames.count (key) == 0 && (key.rfind ("params.", 0) != 0 || key.size() == 7))
            fail (matrixAt.child (key), "expected sample_rate, block_size, channels, non_realtime or params.<name>");

        requireList (matrixAt.child (key));
        combinations *= matrixAt.child (key).node.size();
    }

    if (matrixKeys.empty() || combinations > 64)
        fail (matrixAt, std::to_string (combinations) + " combinations; a matrix can have from 1 to 64");

    std::vector<Scenario> expanded;

    for (size_t combination = 0; combination < combinations; ++combination)
    {
        auto variant = scenario;
        std::string suffix;
        auto remaining = combination;

        // The last key changes fastest, like the digits of a number.
        std::vector<size_t> choice (matrixKeys.size());

        for (auto k = matrixKeys.size(); k-- > 0;)
        {
            const auto count = matrixAt.child (matrixKeys[k]).node.size();
            choice[k] = remaining % count;
            remaining /= count;
        }

        for (size_t k = 0; k < matrixKeys.size(); ++k)
        {
            const auto& key = matrixKeys[k];
            const auto value = matrixAt.child (key).item (choice[k]);
            const auto isParam = kMatrixShortNames.count (key) == 0;

            if (! value.exists() || ! value.node.IsScalar())
                fail (value, "expected a single value, got " + describe (value.node));

            if (isParam)
                setParam (variant.params, parseParam (key.substr (7), value));
            else
                applySetting (variant, key, value);

            suffix += (suffix.empty() ? "" : ",") + (isParam ? key.substr (7) : kMatrixShortNames.at (key)) + "=" + value.node.Scalar();
        }

        variant.id += "@" + suffix;
        validate (variant, at);
        expanded.push_back (std::move (variant));
    }

    return expanded;
}
} // namespace

//==============================================================================
const std::map<std::string, std::vector<std::string>>& metricFields()
{
    static const std::map<std::string, std::vector<std::string>> fields {
        { "level", { "peak_dbfs", "rms_dbfs", "gain_db", "dc" } },
        { "true_peak", { "true_peak_dbtp", "sample_peak_dbfs" } },
        { "harmonics", { "f0_hz", "thd_db", "thdn_db", "h_dbc" } },
        { "aliasing", { "aliasing_dbc", "components", "skipped" } },
        { "frequency_response", { "freqs_hz", "magnitude_db", "phase_deg", "gain_1k_db" } },
        { "latency", { "reported_samples", "measured_samples", "mismatch_samples" } },
        { "determinism", { "bit_identical", "max_diff_dbfs" } },
        { "block_invariance", { "max_diff_dbfs", "rms_diff_dbfs" } },
        { "cpu", { "realtime_factor", "core_percent", "p95_block_us" } },
        { "silence", { "noise_floor_dbfs", "peak_dbfs" } } };

    return fields;
}

Config parseConfig (const std::string& yaml, const std::filesystem::path& baseDir)
{
    At root;

    try
    {
        root.node = YAML::Load (yaml);
    }
    catch (const YAML::Exception& e)
    {
        throw Error (ErrorCode::config, "line " + std::to_string (e.mark.line + 1) + ", column " + std::to_string (e.mark.column + 1)
                                            + ": not valid YAML: " + e.msg);
    }

    checkKeys (root, { "version", "plugin", "defaults", "tolerances", "scenarios" });

    const auto version = root.child ("version");

    if (! version.exists())
        fail (version, "missing: add \"version: 1\"");

    if (wholeNumber (version, 0) != 1)
        fail (version, "this Sonolint reads version 1, got " + describe (version.node));

    Config config;

    if (const auto plugin = root.child ("plugin"); plugin.exists())
    {
        checkKeys (plugin, { "path", "name" });

        if (const auto path = plugin.child ("path"); path.exists())
            config.pluginPath = resolve (baseDir, path);

        if (const auto name = plugin.child ("name"); name.exists())
            config.pluginName = text (name);
    }

    Scenario defaults;

    if (const auto defaultsAt = root.child ("defaults"); defaultsAt.exists())
    {
        checkKeys (defaultsAt, kSettingKeys);
        applySettings (defaults, defaultsAt);
    }

    config.tolerances = parseTolerances (root.child ("tolerances"));

    const auto scenarios = root.child ("scenarios");

    if (! scenarios.exists())
        fail (scenarios, "missing: a config needs at least one scenario");

    requireList (scenarios);
    std::set<std::string> ids;

    for (size_t i = 0; i < scenarios.node.size(); ++i)
    {
        for (auto& scenario : parseScenario (scenarios.item (i), defaults, baseDir))
        {
            if (! ids.insert (scenario.id).second)
                fail (scenarios.item (i).child ("id"), "the id \"" + scenario.id + "\" is used twice");

            config.scenarios.push_back (std::move (scenario));
        }
    }

    return config;
}

Config loadConfig (const std::filesystem::path& file)
{
    std::ifstream stream (file, std::ios::binary);

    if (! stream)
        throw Error (ErrorCode::config, "could not read the config file " + toUtf8 (file));

    std::ostringstream content;
    content << stream.rdbuf();
    return parseConfig (content.str(), file.parent_path());
}

SignalSpec parseSignalText (const std::string& signalText)
{
    // "sine:freq_hz=1000,level_dbfs=-6" becomes the YAML map { type: sine, freq_hz: 1000, level_dbfs: -6 }.
    const auto colon = signalText.find (':');
    auto yaml = "{ type: " + signalText.substr (0, colon);

    if (colon != std::string::npos)
    {
        std::istringstream fields (signalText.substr (colon + 1));

        for (std::string field; std::getline (fields, field, ',');)
        {
            const auto equals = field.find ('=');

            if (equals == std::string::npos)
                throw Error (ErrorCode::config, "--signal: expected name=value, got \"" + field + "\"");

            yaml += ", " + field.substr (0, equals) + ": " + field.substr (equals + 1);
        }
    }

    At at;
    at.path = "--signal";

    try
    {
        at.node = YAML::Load (yaml + " }");
    }
    catch (const YAML::Exception&)
    {
        throw Error (ErrorCode::config, "--signal: expected something like sine:freq_hz=1000,level_dbfs=-6, got \"" + signalText + "\"");
    }

    return parseSignal (at, std::filesystem::current_path());
}

std::vector<Scenario> filterScenarios (const std::vector<Scenario>& scenarios, const std::vector<std::string>& globs)
{
    if (globs.empty())
        return scenarios;

    std::vector<Scenario> kept;

    for (const auto& scenario : scenarios)
        if (std::any_of (globs.begin(), globs.end(), [&] (const std::string& glob) { return globMatch (glob, scenario.id); }))
            kept.push_back (scenario);

    return kept;
}
} // namespace sonolint
