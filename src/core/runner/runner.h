#pragma once

#include "core/config/config.h"
#include "core/runner/renderer.h"
#include "core/signals/signal.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace sonolint
{
struct RunOptions
{
    // Progress, one line per scenario. May be empty.
    std::function<void (const std::string&)> log;

    // Called from another thread when a scenario runs past its timeout_s, with the
    // results so far and that scenario marked as an error. The render is stuck inside
    // the plugin, so the only way out is to save this and end the process.
    std::function<void (const nlohmann::json&)> onTimeout;
};

// Runs the scenarios one after another, each on a fresh plugin instance: makes the
// signal, renders it, checks the output, measures and evaluates `expect`.
// Returns the results document (schema sonolint.results/v1).
//
// A parameter that the plugin doesn't have throws Error (config) before anything is
// rendered. A problem inside one scenario makes that scenario an `error` and the run
// goes on, except a plugin that can't be loaded: that Error is passed on.
nlohmann::json runScenarios (const std::vector<Scenario>& scenarios, const PluginInfo& plugin, Renderer& renderer,
                             const RunOptions& options = {});

// The test signal of a scenario, the same on every channel. With snapToAnalysisBin
// a sine moves to the nearest bin of the FFT that will analyse it (unless its `snap`
// is off).
Signal makeSignal (const SignalSpec& spec, double sampleRate, int channels, double skipMs, bool snapToAnalysisBin);
} // namespace sonolint
