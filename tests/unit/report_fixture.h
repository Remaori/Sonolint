#pragma once

#include "core/compare/compare.h"
#include "core/report/describe.h"

#include <nlohmann/json.hpp>

namespace test
{
// Results with one scenario of each status, as `sonolint run` writes them after the
// comparison: what the reports are tested on.
inline nlohmann::json reportResults()
{
    auto results = nlohmann::json::parse (R"({
      "schema": "sonolint.results/v1",
      "sonolint_version": "0.1.0",
      "plugin": { "name": "Example", "vendor": "Acme", "version": "1.2.3" },
      "environment": { "platform": "macos-arm64", "os": "macOS 27.2.1", "cpu": "Apple M5" },
      "scenarios": [
        { "id": "fat50-response", "status": "changed", "description": "Fatness 50 % keeps the lows",
          "config": { "sample_rate": 48000.0, "block_size": 512, "channels": 2, "params": { "Fatness": 0.5 },
                      "signal": { "type": "stepped_sine", "level_dbfs": -24.0 }, "metrics": [ "frequency_response" ] },
          "metrics": { "frequency_response": {
              "freqs_hz": [ 31.5, 1000.0, 16000.0 ], "magnitude_db": [ 0.31, 0.0, -2.6 ], "phase_deg": [ 1.0, -10.0, -170.0 ], "gain_1k_db": 0.0,
              "per_channel": [
                { "freqs_hz": [ 31.5, 1000.0, 16000.0 ], "magnitude_db": [ 0.3, 0.0, -2.6 ], "phase_deg": [ 1.0, -10.0, -170.0 ], "gain_1k_db": 0.0 },
                { "freqs_hz": [ 31.5, 1000.0, 16000.0 ], "magnitude_db": [ 0.31, 0.0, -2.6 ], "phase_deg": [ 1.0, -10.0, -170.0 ], "gain_1k_db": 0.0 } ] } },
          "assertions": [],
          "comparison": [
            { "metric": "frequency_response.freqs_hz", "channel": 1, "status": "pass", "actual": [ 31.5, 1000.0, 16000.0 ], "baseline": [ 31.5, 1000.0, 16000.0 ], "delta": 0.0, "points_outside": 0, "tolerance": { "abs": 0.0, "rel": 0.0 } },
            { "metric": "frequency_response.freqs_hz", "channel": 2, "status": "pass", "actual": [ 31.5, 1000.0, 16000.0 ], "baseline": [ 31.5, 1000.0, 16000.0 ], "delta": 0.0, "points_outside": 0, "tolerance": { "abs": 0.0, "rel": 0.0 } },
            { "metric": "frequency_response.magnitude_db", "channel": 1, "status": "changed", "actual": [ 0.3, 0.0, -2.6 ], "baseline": [ 0.3, 0.0, -2.2 ], "delta": -0.4, "points_outside": 1, "tolerance": { "abs": 0.1, "rel": 0.0 } },
            { "metric": "frequency_response.magnitude_db", "channel": 2, "status": "changed", "actual": [ 0.31, 0.0, -2.6 ], "baseline": [ 0.3, 0.0, -2.2 ], "delta": -0.4, "points_outside": 1, "tolerance": { "abs": 0.1, "rel": 0.0 } },
            { "metric": "frequency_response.phase_deg", "channel": 1, "status": "changed", "report_only": true, "actual": [ 1.0, -10.0, -170.0 ], "baseline": [ 1.0, -10.0, -160.0 ], "delta": -10.0, "points_outside": 1, "tolerance": { "abs": 2.0, "rel": 0.0 } },
            { "metric": "frequency_response.phase_deg", "channel": 2, "status": "changed", "report_only": true, "actual": [ 1.0, -10.0, -170.0 ], "baseline": [ 1.0, -10.0, -160.0 ], "delta": -10.0, "points_outside": 1, "tolerance": { "abs": 2.0, "rel": 0.0 } },
            { "metric": "frequency_response.gain_1k_db", "channel": 1, "status": "pass", "actual": 0.0, "baseline": 0.0, "delta": 0.0, "tolerance": { "abs": 0.1, "rel": 0.0 } },
            { "metric": "frequency_response.gain_1k_db", "channel": 2, "status": "pass", "actual": 0.0, "baseline": 0.0, "delta": 0.0, "tolerance": { "abs": 0.1, "rel": 0.0 } } ] },

        { "id": "transparent-1k", "status": "assertion_failed",
          "config": { "sample_rate": 48000.0, "block_size": 512, "channels": 1, "params": { "Output": { "text": "0 dB" } },
                      "signal": { "type": "sine", "freq_hz": 1000.0, "level_dbfs": -6.0 }, "metrics": [ "level", "harmonics" ] },
          "metrics": {
            "level": { "gain_db": -0.08, "peak_dbfs": -6.08, "per_channel": [ { "gain_db": -0.08, "peak_dbfs": -6.08 } ] },
            "harmonics": { "thd_db": -61.2, "h_dbc": [ -62.0, -70.5, null ], "per_channel": [ { "thd_db": -61.2, "h_dbc": [ -62.0, -70.5, null ] } ] } },
          "assertions": [
            { "metric": "level.gain_db", "rule": { "approx": 0.0, "tol": 0.05 }, "actual": -0.08, "passed": false, "default": false },
            { "metric": "harmonics.thd_db", "rule": { "max": -60.0 }, "actual": -61.2, "passed": true, "default": false } ],
          "comparison": [
            { "metric": "harmonics.h_dbc", "channel": 1, "status": "pass", "actual": [ -62.0, -70.5, null ], "baseline": [ -62.5, -70.0, null ], "delta": 0.5, "points_outside": 0, "tolerance": { "abs": 1.5, "rel": 0.0 } },
            { "metric": "harmonics.thd_db", "channel": 1, "status": "pass", "actual": -61.2, "baseline": -61.0, "delta": -0.2, "tolerance": { "abs": 1.0, "rel": 0.0 } },
            { "metric": "level.gain_db", "channel": 1, "status": "pass", "actual": -0.08, "baseline": -0.07, "delta": -0.01, "tolerance": { "abs": 0.05, "rel": 0.0 } },
            { "metric": "level.peak_dbfs", "channel": 1, "status": "pass", "actual": -6.08, "baseline": -6.07, "delta": -0.01, "tolerance": { "abs": 0.1, "rel": 0.0 } } ] },

        { "id": "thd-hot", "status": "regressed",
          "metrics": { "harmonics": { "thd_db": -40.0, "per_channel": [ { "thd_db": -40.0 } ] } },
          "assertions": [],
          "comparison": [
            { "metric": "harmonics.thd_db", "channel": 1, "status": "regressed", "actual": -40.0, "baseline": -45.5, "delta": 5.5, "tolerance": { "abs": 1.0, "rel": 0.0 } } ] },

        { "id": "clip-4x", "status": "improved",
          "metrics": { "aliasing": { "aliasing_dbc": -50.0, "per_channel": [ { "aliasing_dbc": -50.0 } ] } },
          "assertions": [],
          "comparison": [
            { "metric": "aliasing.aliasing_dbc", "channel": 1, "status": "improved", "actual": -50.0, "baseline": -45.0, "delta": -5.0, "tolerance": { "abs": 2.0, "rel": 0.025 } } ] },

        { "id": "nan", "status": "error", "error": "the output has NaN or infinite samples" },

        { "id": "quiet", "status": "removed" },

        { "id": "robustness", "status": "new",
          "metrics": { "latency": { "measured_samples": 122.0, "per_channel": [ { "measured_samples": 122.0 } ] },
                       "cpu": { "realtime_factor": 0.0065, "core_percent": 0.65, "p95_block_us": 75.3 } },
          "assertions": [],
          "comparison": [ { "metric": "latency.measured_samples", "channel": 1, "status": "new", "actual": 122.0, "tolerance": { "abs": 0.5, "rel": 0.0 } } ] },

        { "id": "steady", "status": "pass", "warnings": [ "the plugin changed its latency during the render, from 0 to 64 samples" ],
          "metrics": { "silence": { "noise_floor_dbfs": -300.0, "per_channel": [ { "noise_floor_dbfs": -300.0 } ] } },
          "assertions": [],
          "comparison": [ { "metric": "silence.noise_floor_dbfs", "channel": 1, "status": "pass", "actual": -300.0, "baseline": -300.0, "delta": 0.0, "tolerance": { "abs": 1.0, "rel": 0.0 } } ] }
      ] })");

    results["summary"] = sonolint::summarise (results["scenarios"]);
    return results;
}

inline sonolint::ReportInfo reportInfo()
{
    return { "https://github.com/Remaori/Heft/actions/runs/123", "0123456789abcdef0123456789abcdef01234567", "main" };
}
} // namespace test
