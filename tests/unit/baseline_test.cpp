#include "core/compare/baseline.h"
#include "core/compare/catalog.h"
#include "core/error.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace
{
// Results with every kind of value: dB, Hz, samples, degrees, linear values, counts,
// yes/no, curves with gaps, two channels, CPU, and a scenario that ended in error.
json fixedResults()
{
    return json::parse (R"({
      "plugin": { "name": "Example", "vendor": "Acme", "version": "1.2.3" },
      "scenarios": [
        { "id": "stereo-1k", "status": "pass", "metrics": {
            "level": { "gain_db": 0.0, "per_channel": [
              { "peak_dbfs": -6.0205999132796, "rms_dbfs": -9.0308998699194, "gain_db": -0.00042, "dc": 0.0000123456789 },
              { "peak_dbfs": -6.5205999132796, "rms_dbfs": -9.5308998699194, "gain_db": -0.50042, "dc": -0.25 } ] },
            "harmonics": { "per_channel": [
              { "f0_hz": 999.755859375, "thd_db": -39.956786515592, "thdn_db": -39.95, "h_dbc": [ -40.000000260661, -59.999999529224, null ] },
              { "f0_hz": 999.755859375, "thd_db": -150.0, "thdn_db": -131.82, "h_dbc": [ -150.0, -150.0, null ] } ] },
            "cpu": { "realtime_factor": 0.0123, "core_percent": 1.23, "p95_block_us": 75.29 } } },
        { "id": "mono-response", "status": "assertion_failed", "metrics": {
            "frequency_response": { "per_channel": [
              { "freqs_hz": [ 31.5, 1000.0, 16000.0 ], "magnitude_db": [ 0.0004, -3.0102999566, -23.9876 ],
                "phase_deg": [ -0.25, -45.0001, -179.9996 ], "gain_1k_db": -3.0102999566 } ] },
            "latency": { "per_channel": [ { "reported_samples": 122, "measured_samples": 121.36789, "mismatch_samples": -0.63211 } ] },
            "determinism": { "per_channel": [ { "bit_identical": true, "max_diff_dbfs": -300.0 } ] },
            "aliasing": { "per_channel": [ { "aliasing_dbc": -79.7961, "components": 23, "skipped": 0 } ] } } },
        { "id": "broken", "status": "error", "error": "the output has NaN or infinite samples" }
      ] })");
}
} // namespace

TEST_CASE ("baseline: values are rounded by unit, and -0 is written as 0")
{
    using sonolint::Unit;
    using sonolint::roundForBaseline;

    CHECK (roundForBaseline (-6.0205999, Unit::db) == -6.021);
    CHECK (roundForBaseline (121.36789, Unit::samples) == 121.368);
    CHECK (roundForBaseline (-45.0001, Unit::degrees) == -45.0);
    CHECK (roundForBaseline (999.755859375, Unit::hz) == 999.76);
    CHECK (roundForBaseline (0.0000123456789, Unit::linear) == 1.23457e-5);
    CHECK (roundForBaseline (-0.25, Unit::linear) == -0.25);
    CHECK (roundForBaseline (0.0, Unit::linear) == 0.0);
    CHECK (roundForBaseline (122, Unit::samples) == 122);
    CHECK (roundForBaseline (true, Unit::flag) == true);
    CHECK (roundForBaseline (json { -40.0000003, nullptr }, Unit::db) == json { -40.0, nullptr });

    const auto negativeZero = roundForBaseline (-0.00042, Unit::db);
    CHECK (negativeZero.dump() == "0.0");
}

TEST_CASE ("baseline: the file is byte for byte the golden one")
{
    const auto baseline = sonolint::updateBaseline (json(), fixedResults(), "macos-arm64", {}, "0.1.0");

    std::ifstream golden (std::filesystem::path (SONOLINT_GOLDEN) / "baseline.json", std::ios::binary);
    REQUIRE (golden);
    std::ostringstream expected;
    expected << golden.rdbuf();

    CHECK (sonolint::formatBaseline (baseline) == expected.str());
}

TEST_CASE ("baseline: CPU and scenarios that ended in error are not stored")
{
    const auto baseline = sonolint::updateBaseline (json(), fixedResults(), "macos-arm64", {}, "0.1.0");
    const auto& scenarios = baseline["platforms"]["macos-arm64"]["scenarios"];

    CHECK_FALSE (scenarios["stereo-1k"].contains ("cpu"));
    CHECK_FALSE (scenarios.contains ("broken"));
    CHECK (scenarios["stereo-1k"]["level"].size() == 2);   // one entry per channel, no summary value
}

TEST_CASE ("baseline: an update replaces only this platform, and with filters only those scenarios")
{
    auto existing = sonolint::updateBaseline (json(), fixedResults(), "windows-x64", {}, "0.1.0");
    existing["platforms"]["macos-arm64"]["scenarios"] = { { "stereo-1k", { { "old", true } } },
                                                          { "mono-response", { { "old", true } } },
                                                          { "gone", { { "old", true } } },
                                                          { "broken", { { "old", true } } } };

    SECTION ("without filters the section is replaced, but a scenario in error keeps what it had")
    {
        const auto updated = sonolint::updateBaseline (existing, fixedResults(), "macos-arm64", {}, "0.2.0");
        const auto& scenarios = updated["platforms"]["macos-arm64"]["scenarios"];

        CHECK_FALSE (scenarios["stereo-1k"].contains ("old"));
        CHECK_FALSE (scenarios["mono-response"].contains ("old"));
        CHECK_FALSE (scenarios.contains ("gone"));
        CHECK (scenarios["broken"] == json { { "old", true } });
        CHECK (updated["platforms"]["windows-x64"] == existing["platforms"]["windows-x64"]);
        CHECK (updated["sonolint_version"] == "0.2.0");
    }

    SECTION ("with filters the rest stays as it was")
    {
        const auto updated = sonolint::updateBaseline (existing, fixedResults(), "macos-arm64", { "stereo*" }, "0.2.0");
        const auto& scenarios = updated["platforms"]["macos-arm64"]["scenarios"];

        CHECK_FALSE (scenarios["stereo-1k"].contains ("old"));
        CHECK (scenarios["gone"] == json { { "old", true } });

        // A scenario outside the filter that ran anyway is still written: it was measured.
        CHECK_FALSE (scenarios["mono-response"].contains ("old"));
    }

    CHECK (sonolint::platformScenarios (existing, "windows-x64").contains ("stereo-1k"));
    CHECK (sonolint::platformScenarios (existing, "macos-x64").is_null());
    CHECK (sonolint::platformScenarios (json(), "macos-x64").is_null());
}

TEST_CASE ("baseline: saved and loaded back unchanged; a missing file is null and a wrong one an error")
{
    const auto folder = std::filesystem::temp_directory_path() / "sonolint_baseline_test";
    std::filesystem::remove_all (folder);
    const auto file = folder / "sonolint" / "baseline.json";

    CHECK (sonolint::loadBaseline (file).is_null());

    const auto baseline = sonolint::updateBaseline (json(), fixedResults(), "macos-arm64", {}, "0.1.0");
    sonolint::saveBaseline (file, baseline);
    CHECK (sonolint::loadBaseline (file) == baseline);

    std::ofstream (folder / "wrong.json") << R"({ "schema": "something.else/v1" })";

    try
    {
        sonolint::loadBaseline (folder / "wrong.json");
        FAIL ("a file that isn't a baseline must be rejected");
    }
    catch (const sonolint::Error& e)
    {
        CHECK (e.code == sonolint::ErrorCode::config);
    }

    std::filesystem::remove_all (folder);
}
