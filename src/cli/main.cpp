#include "core/compare/baseline.h"
#include "core/compare/compare.h"
#include "core/config/config.h"
#include "core/error.h"
#include "core/io/path_text.h"
#include "core/io/wav.h"
#include "core/platform.h"
#include "cli/terminal.h"
#include "core/report/html.h"
#include "core/report/junit.h"
#include "core/report/markdown.h"
#include "core/report/param_list.h"
#include "core/report/summary.h"
#include "core/runner/runner.h"
#include "host/plugin_loader.h"
#include "host/vst3_renderer.h"

#include <CLI/CLI.hpp>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
using json = nlohmann::json;
using sonolint::Error;
using sonolint::ErrorCode;

struct RunArgs
{
    std::filesystem::path config = "sonolint.yml", plugin, out = "sonolint-out", baseline = "sonolint/baseline.json";
    std::vector<std::string> filters, environment;
    std::string failOn = "regressions", formats = "md,html,json,junit", artifactUrl, commit, mergedInto;
    bool verbose = false, noCompare = false, saveAudio = false;
};

struct RenderArgs
{
    std::filesystem::path plugin, out;
    std::string signal;
    std::vector<std::string> params;
    double sampleRate = 48000.0;
    int blockSize = 512;
};

// --environment KEY=VALUE: what the caller knows about the machine and Sonolint can't,
// such as the image of a CI runner.
json parseEnvironment (const std::vector<std::string>& entries)
{
    auto extra = json::object();

    for (const auto& entry : entries)
    {
        const auto equals = entry.find ('=');
        const auto key = entry.substr (0, equals);

        if (equals == std::string::npos || key.empty())
            throw Error (ErrorCode::config, "--environment: expected KEY=VALUE, got \"" + entry + "\"");

        if (key == "platform" || key == "os" || key == "cpu")
            throw Error (ErrorCode::config, "--environment: Sonolint measures \"" + key + "\" itself");

        extra[key] = entry.substr (equals + 1);
    }

    return extra;
}

// What only the executable knows (its version and the machine), and what --environment adds.
void addEnvironment (json& results, const std::vector<std::string>& extra)
{
    results["sonolint_version"] = SONOLINT_VERSION;
    results["environment"] = { { "platform", sonolint::currentPlatform() },
                               { "os", juce::SystemStats::getOperatingSystemName().toStdString() },
                               { "cpu", juce::SystemStats::getCpuModel().toStdString() } };
    results["environment"].update (parseEnvironment (extra));
}

void writeText (const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::create_directories (file.parent_path());
    std::ofstream stream (file, std::ios::binary);   // binary: LF line endings on Windows too
    stream << text;

    if (! stream)
        throw Error (ErrorCode::config, "could not write " + sonolint::toUtf8 (file));
}

void writeResults (const std::filesystem::path& outDir, json results, const std::vector<std::string>& extra)
{
    addEnvironment (results, extra);
    writeText (outDir / "results.json", results.dump (2) + "\n");
}

// --format: which of md, html, json and junit to write.
std::vector<std::string> parseFormats (const std::string& text)
{
    std::vector<std::string> formats;
    std::istringstream list (text);

    for (std::string format; std::getline (list, format, ',');)
    {
        format.erase (0, format.find_first_not_of (' '));
        format.erase (format.find_last_not_of (' ') + 1);

        if (format != "md" && format != "html" && format != "json" && format != "junit")
            throw Error (ErrorCode::config, "--format: unknown format \"" + format + "\"; use md, html, json and junit, separated by commas");

        formats.push_back (format);
    }

    if (formats.empty())
        throw Error (ErrorCode::config, "--format: name at least one of md, html, json and junit");

    return formats;
}

// The scenarios of the config that match --filter, run on the plugin. What `run` and
// `baseline update` share.
json runConfig (const RunArgs& args, const sonolint::Config& config)
{
    const auto pluginPath = args.plugin.empty() ? config.pluginPath : args.plugin;

    if (pluginPath.empty())
        throw Error (ErrorCode::config, "no plugin to test: set plugin.path in the config or pass --plugin");

    const auto scenarios = sonolint::filterScenarios (config.scenarios, args.filters);

    if (scenarios.empty())
        throw Error (ErrorCode::config, "no scenario matches --filter");

    // The main thread acts as the message thread: plugins expect to be created on it.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto plugin = sonolint::inspectPlugin (pluginPath, config.pluginName);
    sonolint::Vst3Renderer renderer (pluginPath, config.pluginName);

    sonolint::RunOptions options;

    if (args.verbose)
        options.log = [] (const std::string& line) { std::cerr << line << std::endl; };

    if (args.saveAudio)
        options.onAudio = [&] (const std::string& id, const sonolint::AudioBuffer& input, const sonolint::AudioBuffer& output)
        {
            std::filesystem::create_directories (args.out / "audio");
            sonolint::writeWav (args.out / "audio" / sonolint::fromUtf8 (id + ".input.wav"), input);
            sonolint::writeWav (args.out / "audio" / sonolint::fromUtf8 (id + ".output.wav"), output);
        };

    // A render that hangs is stuck inside the plugin: all that is left is to save what
    // was measured and end the process.
    options.onTimeout = [&] (const json& partial)
    {
        writeResults (args.out, partial, args.environment);
        std::cerr << "error: a scenario ran past its timeout; the results so far are in " << sonolint::toUtf8 (args.out / "results.json") << std::endl;
        std::_Exit (1);
    };

    return sonolint::runScenarios (scenarios, plugin, renderer, options);
}

int run (const RunArgs& args)
{
    const auto formats = parseFormats (args.formats);
    parseEnvironment (args.environment);   // a mistake stops the run before anything is rendered
    const auto config = sonolint::loadConfig (args.config);
    const auto platform = sonolint::currentPlatform();
    const auto baseline = args.noCompare ? json() : sonolint::loadBaseline (args.baseline);   // a broken file stops here, before rendering
    const auto section = sonolint::platformScenarios (baseline, platform);

    auto results = runConfig (args, config);

    if (! args.noCompare)
    {
        sonolint::compareWithBaseline (results, section, config.scenarios, config.tolerances, args.filters);
        results["baseline"] = { { "file", sonolint::toUtf8 (args.baseline) }, { "platform", platform } };

        if (baseline.is_null())
            std::cerr << "note: there is no baseline at " << sonolint::toUtf8 (args.baseline)
                      << ", so every value is new; create it with sonolint baseline update" << std::endl;
        else if (section.is_null())
            std::cerr << "note: the baseline has no values for " << platform << " yet, so every value is new; add them with sonolint baseline update"
                      << std::endl;
    }

    addEnvironment (results, args.environment);
    const sonolint::ReportInfo info { args.artifactUrl, args.commit, args.mergedInto };
    const auto wants = [&] (const char* format) { return std::find (formats.begin(), formats.end(), format) != formats.end(); };

    if (wants ("json"))   writeText (args.out / "results.json", results.dump (2) + "\n");
    if (wants ("md"))     writeText (args.out / "report.md", sonolint::formatMarkdown (results, info));
    if (wants ("html"))   writeText (args.out / "report.html", sonolint::formatHtml (results, info));
    if (wants ("junit"))  writeText (args.out / "junit.xml", sonolint::formatJunit (results));

    const auto noColour = juce::SystemStats::getEnvironmentVariable ("NO_COLOR", {});
    std::cout << sonolint::formatSummary (results, noColour.isEmpty() && sonolint::stdoutIsColourTerminal());

    const auto policy = args.failOn == "none"         ? sonolint::FailOn::none
                      : args.failOn == "assertions"   ? sonolint::FailOn::assertions
                                                      : sonolint::FailOn::regressions;
    return sonolint::fails (results, policy) ? 1 : 0;
}

int updateBaseline (const RunArgs& args)
{
    const auto config = sonolint::loadConfig (args.config);
    const auto existing = sonolint::loadBaseline (args.baseline);
    const auto results = runConfig (args, config);

    sonolint::saveBaseline (args.baseline, sonolint::updateBaseline (existing, results, sonolint::currentPlatform(), args.filters, SONOLINT_VERSION));
    std::cout << sonolint::formatSummary (results);

    std::vector<std::string> errored;

    for (const auto& scenario : results["scenarios"])
        if (scenario["status"] == "error")
            errored.push_back (scenario["id"].get<std::string>());

    std::cout << "wrote " << results["scenarios"].size() - errored.size() << " scenarios for " << sonolint::currentPlatform() << " to "
              << sonolint::toUtf8 (args.baseline) << "\n";

    if (errored.empty())
        return 0;

    std::cerr << "error: " << errored.size() << " scenarios ended in error and kept the values they had:";

    for (const auto& id : errored)
        std::cerr << " " << id;

    std::cerr << std::endl;
    return 1;
}

int render (const RenderArgs& args)
{
    if (args.sampleRate <= 0.0 || args.blockSize < 1)
        throw Error (ErrorCode::config, "--sample-rate and --block-size must be above 0");

    constexpr int channels = 2;
    const auto signal = sonolint::makeSignal (sonolint::parseSignalText (args.signal), args.sampleRate, channels, 100.0, false);

    sonolint::RenderRequest request;
    request.sampleRate          = args.sampleRate;
    request.channels            = channels;
    request.blocks.maxBlockSize = args.blockSize;
    request.warmupSamples       = (int) (0.2 * args.sampleRate);
    request.tailSamples         = (int) (0.1 * args.sampleRate);
    request.input               = signal.audio;

    for (const auto& param : args.params)
    {
        const auto equals = param.find ('=');

        if (equals == std::string::npos)
            throw Error (ErrorCode::config, "--param: expected NAME=VALUE, got \"" + param + "\"");

        // A number is a normalized value; anything else is text in the plugin's own units.
        const auto value = param.substr (equals + 1);
        char* end = nullptr;
        const auto number = std::strtod (value.c_str(), &end);

        if (value.empty() || *end != '\0')
            request.params.push_back ({ param.substr (0, equals), value });
        else if (number >= 0.0 && number <= 1.0)
            request.params.push_back ({ param.substr (0, equals), number });
        else
            throw Error (ErrorCode::config, "--param " + param + ": a number is a normalized value from 0 to 1; for the plugin's own units "
                                                "add the unit, like \"6 dB\"");
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto result = sonolint::Vst3Renderer (args.plugin).render (request);
    sonolint::writeWav (args.out, result.output);

    std::cout << "wrote " << sonolint::toUtf8 (args.out) << ": " << (double) result.output.numSamples() / args.sampleRate
              << " s, reported latency " << result.reportedLatencySamples << " samples\n";
    return 0;
}

int listParams (const std::filesystem::path& pluginPath, bool asJson)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto plugin = sonolint::inspectPlugin (pluginPath);
    std::cout << (asJson ? sonolint::formatParamJson (plugin) : sonolint::formatParamTable (plugin));
    return 0;
}

// The options that `run` and `baseline update` share.
void addScenarioOptions (CLI::App& command, RunArgs& args)
{
    command.add_option ("--config", args.config, "The configuration file")->capture_default_str()->type_name ("PATH");
    command.add_option ("--plugin", args.plugin, "Path to the .vst3; replaces plugin.path of the config")->type_name ("PATH");
    command.add_option ("--baseline", args.baseline, "The baseline file")->capture_default_str()->type_name ("PATH");
    command.add_option ("--filter", args.filters, "Only the scenarios whose id matches this glob; can be repeated")->type_name ("GLOB");
    command.add_flag ("--verbose", args.verbose, "Print each scenario as it finishes");
}
} // namespace

int main (int argc, char* argv[])
{
    CLI::App app { "Sonolint runs your compiled VST3 like a DAW would, measures how it sounds and flags changes." };
    argv = app.ensure_utf8 (argv);   // Windows passes the arguments in the ANSI code page
    app.require_subcommand (1);

    RunArgs runArgs;
    auto* runCommand = app.add_subcommand ("run", "Run the scenarios of sonolint.yml, compare them with the baseline and write results.json");
    addScenarioOptions (*runCommand, runArgs);
    runCommand->add_option ("--out", runArgs.out, "Folder for the results")->capture_default_str()->type_name ("DIR");
    runCommand->add_option ("--fail-on", runArgs.failOn, "What makes the run fail")
        ->check (CLI::IsMember ({ "regressions", "assertions", "none" }))
        ->capture_default_str();
    runCommand->add_flag ("--no-compare", runArgs.noCompare, "Don't compare with the baseline");
    runCommand->add_option ("--format", runArgs.formats, "Which files to write, separated by commas: md, html, json, junit")->capture_default_str();
    runCommand->add_flag ("--save-audio", runArgs.saveAudio, "Also write the input and the output of every scenario to <out>/audio as WAV files");
    runCommand->add_option ("--artifact-url", runArgs.artifactUrl, "Where the full report can be downloaded, for the footer of report.md")->type_name ("URL");
    runCommand->add_option ("--commit", runArgs.commit, "The commit that was measured, for the footers of the reports")->type_name ("SHA");
    runCommand->add_option ("--merged-into", runArgs.mergedInto, "The branch the commit was merged into before measuring, for the footers")
        ->type_name ("BRANCH");
    runCommand->add_option ("--environment", runArgs.environment, "Something about the machine to record in results.json, such as a CI runner's image; can be repeated")
        ->type_name ("KEY=VALUE");
    runCommand->footer ("Example:\n  sonolint run --plugin build/Heft_artefacts/Release/VST3/Heft.vst3");

    RunArgs updateArgs;
    auto* baselineCommand = app.add_subcommand ("baseline", "Manage the baseline");
    baselineCommand->require_subcommand (1);
    auto* updateCommand = baselineCommand->add_subcommand ("update", "Run the scenarios and write their values as this platform's baseline");
    addScenarioOptions (*updateCommand, updateArgs);
    updateCommand->footer ("Example:\n  sonolint baseline update --plugin build/Heft_artefacts/Release/VST3/Heft.vst3");

    std::filesystem::path listPlugin;
    bool asJson = false;
    auto* listCommand = app.add_subcommand ("list-params", "List the parameters a plugin exposes to the host");
    listCommand->add_option ("--plugin", listPlugin, "Path to the .vst3")->required()->type_name ("PATH");
    listCommand->add_flag ("--json", asJson, "Print JSON instead of a table");
    listCommand->footer ("Example:\n  sonolint list-params --plugin build/Heft_artefacts/Release/VST3/Heft.vst3");

    RenderArgs renderArgs;
    auto* renderCommand = app.add_subcommand ("render", "Render one signal through the plugin to a WAV file, without measuring");
    renderCommand->add_option ("--plugin", renderArgs.plugin, "Path to the .vst3")->required()->type_name ("PATH");
    renderCommand->add_option ("--signal", renderArgs.signal, "The signal, like sine:freq_hz=1000,level_dbfs=-6")->required()->type_name ("SIGNAL");
    renderCommand->add_option ("--out", renderArgs.out, "The WAV file to write")->required()->type_name ("FILE");
    renderCommand->add_option ("--param", renderArgs.params, "A parameter: normalized (Fatness=0.5) or as text (\"Output=-6 dB\"); can be repeated")
        ->type_name ("NAME=VALUE");
    renderCommand->add_option ("--sample-rate", renderArgs.sampleRate, "Sample rate in Hz")->capture_default_str();
    renderCommand->add_option ("--block-size", renderArgs.blockSize, "Largest block passed to the plugin")->capture_default_str();
    renderCommand->footer ("Example:\n  sonolint render --plugin Heft.vst3 --signal \"sine:freq_hz=1000,level_dbfs=-6\" --out out.wav");

    auto* versionCommand = app.add_subcommand ("version", "Print the Sonolint version, commit, JUCE version and build date");
    versionCommand->footer ("Example:\n  sonolint version");

    try
    {
        app.parse (argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        // --help also arrives here, with exit code 0. Anything else is a usage error.
        return app.exit (e) == 0 ? 0 : static_cast<int> (ErrorCode::config);
    }

    try
    {
        if (runCommand->parsed())     return run (runArgs);
        if (updateCommand->parsed())  return updateBaseline (updateArgs);
        if (listCommand->parsed())    return listParams (listPlugin, asJson);
        if (renderCommand->parsed())  return render (renderArgs);

        std::cout << "sonolint " SONOLINT_VERSION " (commit " SONOLINT_COMMIT ", built " SONOLINT_BUILD_DATE ")\n"
                  << juce::SystemStats::getJUCEVersion().toStdString() << "\n";
        return 0;
    }
    catch (const Error& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return static_cast<int> (e.code);
    }
    catch (const std::exception& e)
    {
        std::cerr << "internal error: " << e.what() << "\n";
        return static_cast<int> (ErrorCode::internal);
    }
}
