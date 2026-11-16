#include "core/config/config.h"
#include "core/error.h"
#include "core/io/path_text.h"
#include "core/io/wav.h"
#include "core/platform.h"
#include "core/report/param_list.h"
#include "core/report/summary.h"
#include "core/runner/runner.h"
#include "host/plugin_loader.h"
#include "host/vst3_renderer.h"

#include <CLI/CLI.hpp>
#include <juce_events/juce_events.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
using json = nlohmann::json;
using sonolint::Error;
using sonolint::ErrorCode;

struct RunArgs
{
    std::filesystem::path config = "sonolint.yml", plugin, out = "sonolint-out";
    std::vector<std::string> filters;
    bool verbose = false;
};

struct RenderArgs
{
    std::filesystem::path plugin, out;
    std::string signal;
    std::vector<std::string> params;
    double sampleRate = 48000.0;
    int blockSize = 512;
};

// Adds what only the executable knows (its version, the machine) and writes results.json.
void writeResults (const std::filesystem::path& outDir, json results)
{
    results["sonolint_version"] = SONOLINT_VERSION;
    results["environment"] = { { "platform", sonolint::currentPlatform() },
                               { "os", juce::SystemStats::getOperatingSystemName().toStdString() },
                               { "cpu", juce::SystemStats::getCpuModel().toStdString() } };

    // On GitHub's runners: which image ran this. A new image can move the numbers a little.
    if (const auto image = juce::SystemStats::getEnvironmentVariable ("ImageVersion", {}); image.isNotEmpty())
        results["environment"]["runner_image"] = (juce::SystemStats::getEnvironmentVariable ("ImageOS", {}) + " " + image).toStdString();

    const auto file = outDir / "results.json";
    std::filesystem::create_directories (outDir);
    std::ofstream stream (file, std::ios::binary);
    stream << results.dump (2) << "\n";

    if (! stream)
        throw Error (ErrorCode::config, "could not write " + sonolint::toUtf8 (file));
}

int run (const RunArgs& args)
{
    const auto config = sonolint::loadConfig (args.config);
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

    // A render that hangs is stuck inside the plugin: all that is left is to save what
    // was measured and end the process.
    options.onTimeout = [&] (const json& partial)
    {
        writeResults (args.out, partial);
        std::cerr << "error: a scenario ran past its timeout; the results so far are in " << sonolint::toUtf8 (args.out / "results.json") << std::endl;
        std::_Exit (1);
    };

    const auto results = sonolint::runScenarios (scenarios, plugin, renderer, options);
    writeResults (args.out, results);
    std::cout << sonolint::formatSummary (results);
    return sonolint::hasFailures (results) ? 1 : 0;
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
} // namespace

int main (int argc, char* argv[])
{
    CLI::App app { "Sonolint runs your compiled VST3 like a DAW would, measures how it sounds and flags changes." };
    argv = app.ensure_utf8 (argv);   // Windows passes the arguments in the ANSI code page
    app.require_subcommand (1);

    RunArgs runArgs;
    auto* runCommand = app.add_subcommand ("run", "Run the scenarios of sonolint.yml and write results.json");
    runCommand->add_option ("--config", runArgs.config, "The configuration file")->capture_default_str()->type_name ("PATH");
    runCommand->add_option ("--plugin", runArgs.plugin, "Path to the .vst3; replaces plugin.path of the config")->type_name ("PATH");
    runCommand->add_option ("--out", runArgs.out, "Folder for the results")->capture_default_str()->type_name ("DIR");
    runCommand->add_option ("--filter", runArgs.filters, "Only the scenarios whose id matches this glob; can be repeated")->type_name ("GLOB");
    runCommand->add_flag ("--verbose", runArgs.verbose, "Print each scenario as it finishes");
    runCommand->footer ("Example:\n  sonolint run --plugin build/Heft_artefacts/Release/VST3/Heft.vst3");

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
