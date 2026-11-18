#pragma once

#include "core/report/describe.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sonolint
{
// report.html: one self-contained page that opens without internet (inline CSS, SVG
// charts, no scripts), in a light and a dark theme. For each scenario: its settings,
// its expect rules, every value against the baseline, and the charts of v0.1 (the
// frequency response, and the harmonics H2 to H10).
std::string formatHtml (const nlohmann::json& results, const ReportInfo& info);
} // namespace sonolint
