#include "core/error.h"
#include "core/report/param_list.h"
#include "host/plugin_loader.h"

#include <CLI/CLI.hpp>
#include <juce_events/juce_events.h>

#include <filesystem>
#include <iostream>

int main (int argc, char* argv[])
{
    CLI::App app { "Sonolint runs your compiled VST3 like a DAW would, measures how it sounds and flags changes." };
    argv = app.ensure_utf8 (argv);   // Windows passes the arguments in the ANSI code page
    app.require_subcommand (1);

    auto* version = app.add_subcommand ("version", "Print the Sonolint version, commit, JUCE version and build date");
    version->footer ("Example:\n  sonolint version");

    std::filesystem::path pluginPath;
    bool asJson = false;

    auto* listParams = app.add_subcommand ("list-params", "List the parameters a plugin exposes to the host");
    listParams->add_option ("--plugin", pluginPath, "Path to the .vst3")->required()->type_name ("PATH");
    listParams->add_flag ("--json", asJson, "Print JSON instead of a table");
    listParams->footer ("Example:\n  sonolint list-params --plugin build/Heft_artefacts/Release/VST3/Heft.vst3");

    try
    {
        app.parse (argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        // --help also arrives here, with exit code 0. Anything else is a usage error.
        return app.exit (e) == 0 ? 0 : static_cast<int> (sonolint::ErrorCode::config);
    }

    try
    {
        if (version->parsed())
        {
            std::cout << "sonolint " SONOLINT_VERSION " (commit " SONOLINT_COMMIT ", built " SONOLINT_BUILD_DATE ")\n"
                      << juce::SystemStats::getJUCEVersion().toStdString() << "\n";
            return 0;
        }

        // The main thread acts as the message thread: plugins expect to be created on it.
        const juce::ScopedJuceInitialiser_GUI juceInit;

        const auto plugin = sonolint::inspectPlugin (pluginPath);
        std::cout << (asJson ? sonolint::formatParamJson (plugin) : sonolint::formatParamTable (plugin));
        return 0;
    }
    catch (const sonolint::Error& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return static_cast<int> (e.code);
    }
    catch (const std::exception& e)
    {
        std::cerr << "internal error: " << e.what() << "\n";
        return static_cast<int> (sonolint::ErrorCode::internal);
    }
}
