#include "core/compare/catalog.h"

#include <cmath>

namespace sonolint
{
const std::map<std::string, std::vector<FieldInfo>>& metricCatalog()
{
    using enum Unit;
    using enum Direction;

    static const std::map<std::string, std::vector<FieldInfo>> catalog {
        { "level", { { "peak_dbfs", db, target, 0.1 }, { "rms_dbfs", db, target, 0.05 }, { "gain_db", db, target, 0.05 },
                     { "dc", linear, target, 1.0e-4 } } },
        { "true_peak", { { "true_peak_dbtp", db, target, 0.1 }, { "sample_peak_dbfs", db, target, 0.1 } } },
        { "harmonics", { { "f0_hz", hz, target, 0.0 }, { "thd_db", db, lower, 1.0 }, { "thdn_db", db, lower, 1.0 },
                         { "h_dbc", db, lower, 1.5 } } },
        { "aliasing", { { "aliasing_dbc", db, lower, 2.0 }, { "components", count, target, 0.0 }, { "skipped", count, target, 0.0 } } },
        { "frequency_response", { { "freqs_hz", hz, target, 0.0 }, { "magnitude_db", db, target, 0.1 },
                                  { "phase_deg", degrees, report, 2.0 }, { "gain_1k_db", db, target, 0.1 } } },
        { "latency", { { "reported_samples", samples, target, 0.0 }, { "measured_samples", samples, target, 0.5 },
                       { "mismatch_samples", samples, target, 0.5 } } },
        { "determinism", { { "bit_identical", flag, target, 0.0 }, { "max_diff_dbfs", db, target, 0.0 } } },
        { "block_invariance", { { "max_diff_dbfs", db, lower, 3.0 }, { "rms_diff_dbfs", db, lower, 3.0 } } },
        { "cpu", { { "realtime_factor", linear, report, 0.0 }, { "core_percent", linear, report, 0.0 },
                   { "p95_block_us", linear, report, 0.0 } } },
        { "silence", { { "noise_floor_dbfs", db, lower, 1.0 }, { "peak_dbfs", db, lower, 1.0 } } } };

    return catalog;
}

nlohmann::json roundForBaseline (const nlohmann::json& value, Unit unit)
{
    if (value.is_array())
    {
        auto rounded = nlohmann::json::array();

        for (const auto& point : value)
            rounded.push_back (roundForBaseline (point, unit));

        return rounded;
    }

    if (! value.is_number_float())
        return value;

    const auto x = value.get<double>();
    auto decimals = 3;

    if (unit == Unit::hz)
        decimals = 2;
    else if (unit == Unit::linear)
        decimals = x == 0.0 ? 0 : 6 - (int) std::ceil (std::log10 (std::abs (x)));   // 6 significant figures
    else if (unit == Unit::count || unit == Unit::flag)
        return value;

    const auto scale = std::pow (10.0, decimals);
    const auto rounded = std::round (x * scale) / scale;
    return rounded == 0.0 ? 0.0 : rounded;   // no "-0.0" in the file
}
} // namespace sonolint
