#include "core/report/html.h"

#include "core/compare/compare.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <locale>
#include <sstream>

namespace sonolint
{
namespace
{
using json = nlohmann::json;

// Colours as roles, with the light and the dark theme in one place. "run" and "base"
// are the first two slots of a palette checked for colour blindness in both themes.
constexpr auto kStyle = R"(:root {
  color-scheme: light dark;
  --page: #f9f9f7; --surface: #fcfcfb; --ink: #0b0b0b; --ink-2: #52514e; --muted: #898781;
  --grid: #e1e0d9; --axis: #c3c2b7; --border: rgba(11, 11, 11, 0.10); --hover: rgba(11, 11, 11, 0.05);
  --run: #2a78d6; --base: #eb6834; --good: #0ca30c; --warning: #fab219; --critical: #d03b3b;
}
@media (prefers-color-scheme: dark) {
  :root {
    --page: #0d0d0d; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --axis: #383835; --border: rgba(255, 255, 255, 0.10); --hover: rgba(255, 255, 255, 0.06);
    --run: #3987e5; --base: #d95926;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--page); color: var(--ink); font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 960px; margin: 0 auto; padding: 32px 16px 64px; }
h1 { font-size: 26px; margin: 0 0 4px; }
h2 { font-size: 20px; margin: 0 0 4px; display: flex; gap: 12px; align-items: baseline; flex-wrap: wrap; }
h3 { font-size: 15px; margin: 24px 0 8px; }
p { margin: 4px 0; }
a { color: inherit; }
.meta, .muted { color: var(--ink-2); }
.counts { font-size: 17px; margin: 12px 0 0; }
.card { background: var(--surface); border: 1px solid var(--border); border-radius: 8px; padding: 20px; margin-top: 24px; overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: 14px; }
th, td { text-align: left; padding: 6px 10px 6px 0; border-bottom: 1px solid var(--grid); vertical-align: top; }
th { color: var(--ink-2); font-weight: 600; }
td.num { font-variant-numeric: tabular-nums; white-space: nowrap; }
.status { white-space: nowrap; font-weight: 600; font-size: 14px; }
.status::before { content: ""; display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 6px; background: var(--muted); }
.st-error::before, .st-regressed::before, .st-changed::before, .st-assertion_failed::before, .st-fail::before { background: var(--critical); }
.st-removed::before, .st-warning::before { background: var(--warning); }
.st-improved::before { background: var(--good); }
.st-info::before { background: var(--muted); }
dl { display: grid; grid-template-columns: max-content 1fr; gap: 2px 16px; margin: 12px 0 0; font-size: 14px; }
dt { color: var(--ink-2); }
dd { margin: 0; }
ul.warnings { margin: 8px 0 0; padding-left: 20px; }
.legend { display: flex; gap: 20px; font-size: 13px; color: var(--ink-2); margin: 0 0 4px; }
.legend svg { vertical-align: middle; margin-right: 6px; }
svg.chart { display: block; width: 100%; height: auto; max-width: 720px; }
.chart text { fill: var(--muted); font-size: 11px; font-family: inherit; }
.chart .grid { stroke: var(--grid); stroke-width: 1; }
.chart .axis { stroke: var(--axis); stroke-width: 1; }
.line { fill: none; stroke-width: 2; stroke-linejoin: round; stroke-linecap: round; }
.line.run { stroke: var(--run); }
.line.base { stroke: var(--base); stroke-dasharray: 6 4; }
.moved { fill: var(--run); stroke: var(--surface); stroke-width: 2; }
.bar.run { fill: var(--run); }
.bar.base { fill: var(--base); fill-opacity: 0.18; stroke: var(--base); stroke-width: 1.5; stroke-dasharray: 4 3; }
.hit { fill: transparent; }
.hit:hover { fill: var(--hover); }
details { margin-top: 8px; font-size: 14px; }
summary { cursor: pointer; color: var(--ink-2); }
footer { margin-top: 32px; color: var(--ink-2); font-size: 13px; }
)";

std::string escaped (const std::string& text)
{
    std::string out;

    for (const auto c : text)
    {
        switch (c)
        {
            case '&':   out += "&amp;"; break;
            case '<':   out += "&lt;"; break;
            case '>':   out += "&gt;"; break;
            case '"':   out += "&quot;"; break;
            case '\'':  out += "&#39;"; break;
            default:    out += c; break;
        }
    }

    return out;
}

// A coordinate of the SVG, with one decimal.
std::string at (double value)
{
    std::ostringstream out;
    out.imbue (std::locale::classic());
    out << std::fixed << std::setprecision (1) << value;
    return out.str();
}

// The id of a scenario as an HTML anchor: matrix ids have "@", "=" and ",".
std::string anchor (const std::string& id)
{
    std::string out = "s-";

    for (const auto c : id)
        out += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '_';

    return out;
}

std::string statusBadge (const std::string& status) { return "<span class=\"status st-" + status + "\">" + escaped (status) + "</span>"; }

std::string frequencyText (double hz) { return hz < 1000.0 ? showValue (hz) + " Hz" : showValue (hz / 1000.0) + " kHz"; }

// An axis with round ticks covering [low, high], at most about six of them.
struct Axis
{
    double low, high, step;
};

Axis roundAxis (double low, double high)
{
    if (high - low < 1.0e-9)
    {
        low -= 1.0;
        high += 1.0;
    }

    auto step = 100.0;

    for (const auto candidate : { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0 })
    {
        if ((high - low) / candidate <= 6.0)
        {
            step = candidate;
            break;
        }
    }

    return { std::floor (low / step + 1.0e-9) * step, std::ceil (high / step - 1.0e-9) * step, step };
}

constexpr double kWidth = 640.0, kHeight = 300.0, kLeft = 52.0, kRight = 16.0, kTop = 28.0, kBottom = 40.0;
constexpr double kPlotWidth = kWidth - kLeft - kRight, kPlotHeight = kHeight - kTop - kBottom;

// The horizontal gridlines and their labels.
std::string yGrid (const Axis& axis, const std::function<double (double)>& y, const std::string& unit)
{
    std::string svg;

    for (auto value = axis.low; value <= axis.high + axis.step * 0.5; value += axis.step)
    {
        const auto rounded = std::round (value / axis.step) * axis.step;
        svg += "<line class=\"grid\" x1=\"" + at (kLeft) + "\" x2=\"" + at (kWidth - kRight) + "\" y1=\"" + at (y (rounded)) + "\" y2=\"" + at (y (rounded))
             + "\"/>\n<text x=\"" + at (kLeft - 8) + "\" y=\"" + at (y (rounded) + 4) + "\" text-anchor=\"end\">" + showValue (rounded == 0.0 ? 0.0 : rounded)
             + "</text>\n";
    }

    return svg + "<text x=\"" + at (kLeft - 8) + "\" y=\"" + at (kTop - 14) + "\" text-anchor=\"end\">" + unit + "</text>\n";
}

std::string legend (bool lines, bool hasBaseline)
{
    const auto key = [&] (const std::string& role)
    {
        return lines ? "<svg width=\"24\" height=\"10\" aria-hidden=\"true\"><line class=\"line " + role + "\" x1=\"2\" y1=\"5\" x2=\"22\" y2=\"5\"/></svg>"
                     : "<svg width=\"14\" height=\"14\" aria-hidden=\"true\"><rect class=\"bar " + role + "\" x=\"1\" y=\"1\" width=\"12\" height=\"12\" rx=\"2\"/></svg>";
    };

    return "<div class=\"legend\"><span>" + key ("run") + "This run</span>" + (hasBaseline ? "<span>" + key ("base") + "Baseline</span>" : "") + "</div>\n";
}

// The values behind a chart, so none of them depends on hovering.
std::string valuesTable (const std::vector<std::string>& labels, const json& actual, const json& baseline, const std::string& unit)
{
    std::string rows;

    for (size_t i = 0; i < labels.size(); ++i)
        rows += "<tr><td>" + labels[i] + "</td><td class=\"num\">" + showCell (actual.at (i)) + "</td>"
              + (baseline.is_array() ? "<td class=\"num\">" + showCell (baseline.at (i)) + "</td>" : "") + "</tr>\n";

    return "<details><summary>Values (" + unit + ")</summary>\n<table><tr><th></th><th>This run</th>" + (baseline.is_array() ? "<th>Baseline</th>" : "")
         + "</tr>\n" + rows + "</table></details>\n";
}

// The values of one curve that can be drawn: a number for each point.
std::vector<std::pair<size_t, double>> points (const json& values)
{
    std::vector<std::pair<size_t, double>> out;

    if (values.is_array())
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i].is_number())
                out.emplace_back (i, values[i].get<double>());

    return out;
}

// The frequency response: logarithmic frequency axis, this run solid, the baseline
// dashed, and a dot on each point that moved beyond the tolerance.
std::string responseChart (const ChannelCurve& curve, const json& freqs, const std::string& title)
{
    if (! freqs.is_array() || freqs.empty() || ! curve.actual.is_array() || curve.actual.size() != freqs.size())
        return {};

    const auto hasBaseline = curve.baseline.is_array() && curve.baseline.size() == freqs.size();

    auto fLow = 20.0, fHigh = 20000.0;
    auto low = 0.0, high = 0.0;

    for (size_t i = 0; i < freqs.size(); ++i)
    {
        fLow = std::min (fLow, freqs[i].get<double>());
        fHigh = std::max (fHigh, freqs[i].get<double>());
    }

    for (const auto* values : { &curve.actual, &curve.baseline })
        for (const auto& [i, value] : points (hasBaseline || values == &curve.actual ? *values : json()))
        {
            low = std::min (low, value);
            high = std::max (high, value);
        }

    const auto axis = roundAxis (low, high);
    const auto x = [&] (double hz) { return kLeft + (std::log10 (hz) - std::log10 (fLow)) / (std::log10 (fHigh) - std::log10 (fLow)) * kPlotWidth; };
    const auto y = [&] (double db) { return kTop + (axis.high - db) / (axis.high - axis.low) * kPlotHeight; };

    auto svg = "<svg class=\"chart\" viewBox=\"0 0 " + at (kWidth) + " " + at (kHeight) + "\" role=\"img\" aria-label=\"" + escaped (title) + "\">\n"
             + yGrid (axis, y, "dB");

    for (const auto hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        if (hz < fLow || hz > fHigh)
            continue;

        svg += "<line class=\"grid\" x1=\"" + at (x (hz)) + "\" x2=\"" + at (x (hz)) + "\" y1=\"" + at (kTop) + "\" y2=\"" + at (kTop + kPlotHeight) + "\"/>\n"
               "<text x=\"" + at (x (hz)) + "\" y=\"" + at (kTop + kPlotHeight + 16) + "\" text-anchor=\"middle\">"
             + (hz < 1000.0 ? showValue (hz) : showValue (hz / 1000.0) + "k") + "</text>\n";
    }

    svg += "<line class=\"axis\" x1=\"" + at (kLeft) + "\" x2=\"" + at (kWidth - kRight) + "\" y1=\"" + at (kTop + kPlotHeight) + "\" y2=\""
         + at (kTop + kPlotHeight) + "\"/>\n<text x=\"" + at (kLeft + kPlotWidth / 2) + "\" y=\"" + at (kHeight - 6)
         + "\" text-anchor=\"middle\">Frequency (Hz)</text>\n";

    const auto polyline = [&] (const json& values, const std::string& role)
    {
        std::string coordinates;

        for (const auto& [i, value] : points (values))
            coordinates += (coordinates.empty() ? "" : " ") + at (x (freqs[i].get<double>())) + "," + at (y (value));

        return "<polyline class=\"line " + role + "\" points=\"" + coordinates + "\"/>\n";
    };

    if (hasBaseline)
        svg += polyline (curve.baseline, "base");

    svg += polyline (curve.actual, "run");

    // Hover bands, one per frequency, each with both values in its tooltip.
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const auto hz = freqs[i].get<double>();
        const auto from = i == 0 ? kLeft : (x (freqs[i - 1].get<double>()) + x (hz)) / 2;
        const auto to = i + 1 == freqs.size() ? kWidth - kRight : (x (hz) + x (freqs[i + 1].get<double>())) / 2;
        const auto tip = frequencyText (hz) + ": this run " + showCell (curve.actual[i]) + " dB"
                       + (hasBaseline ? ", baseline " + showCell (curve.baseline[i]) + " dB" : "");

        svg += "<rect class=\"hit\" x=\"" + at (from) + "\" y=\"" + at (kTop) + "\" width=\"" + at (to - from) + "\" height=\"" + at (kPlotHeight)
             + "\"><title>" + escaped (tip) + "</title></rect>\n";
    }

    // A dot on each point that moved beyond the tolerance, when they are a few: if most of
    // the curve moved, the two lines already say so and dots everywhere would be noise.
    if (hasBaseline)
    {
        const auto abs = curve.tolerance.value ("abs", 0.0), rel = curve.tolerance.value ("rel", 0.0);
        std::string dots;
        size_t moved = 0;

        for (const auto& [i, value] : points (curve.actual))
        {
            if (! curve.baseline[i].is_number())
                continue;

            const auto stored = curve.baseline[i].get<double>();

            if (std::abs (std::max (value, -130.0) - std::max (stored, -130.0)) > abs + rel * std::abs (stored) + 1.0e-9)
            {
                dots += "<circle class=\"moved\" cx=\"" + at (x (freqs[i].get<double>())) + "\" cy=\"" + at (y (value)) + "\" r=\"4\"/>\n";
                ++moved;
            }
        }

        if (2 * moved < freqs.size())
            svg += dots;
    }

    std::vector<std::string> labels;

    for (const auto& hz : freqs)
        labels.push_back (frequencyText (hz.get<double>()));

    return "<h3>" + escaped (title) + "</h3>\n" + legend (true, hasBaseline) + svg + "</svg>\n"
         + valuesTable (labels, curve.actual, hasBaseline ? curve.baseline : json(), "dB");
}

// A bar with a rounded top and a square foot, from `foot` up (or down) to `top`.
std::string bar (double x, double width, double foot, double top, const std::string& role, const std::string& tip)
{
    const auto height = std::abs (foot - top);
    const auto r = std::min ({ 4.0, height, width / 2 });
    const auto up = top < foot;
    const auto end = up ? top + r : top - r;
    const auto curve = up ? -r : r;

    return "<path class=\"bar " + role + "\" d=\"M" + at (x) + " " + at (foot) + "V" + at (end) + "Q" + at (x) + " " + at (end + curve) + " " + at (x + r) + " "
         + at (end + curve) + "H" + at (x + width - r) + "Q" + at (x + width) + " " + at (end + curve) + " " + at (x + width) + " " + at (end) + "V" + at (foot)
         + "Z\"><title>" + escaped (tip) + "</title></path>\n";
}

// The harmonics H2 to H10 in dBc, the baseline and this run side by side.
std::string harmonicsChart (const ChannelCurve& curve, const std::string& title)
{
    if (! curve.actual.is_array() || curve.actual.empty())
        return {};

    const auto count = curve.actual.size();
    const auto hasBaseline = curve.baseline.is_array() && curve.baseline.size() == count;

    auto low = 0.0, high = -1000.0;

    for (const auto* values : { &curve.actual, &curve.baseline })
        for (const auto& [i, value] : points (hasBaseline || values == &curve.actual ? *values : json()))
        {
            low = std::min (low, value);
            high = std::max (high, value);
        }

    if (high <= -130.0)   // nothing above the floor of the comparison: an empty chart would say less than this
        return "<h3>" + escaped (title) + "</h3>\n<p class=\"muted\">Every harmonic from H2 to H10 is below -130 dBc"
             + std::string (hasBaseline ? ", in this run and in the baseline." : ".") + "</p>\n";

    // The bars stand on a fixed, deep floor, so their height shows how strong a harmonic is.
    const Axis axis { std::max (-160.0, std::min (-120.0, std::floor (low / 20.0) * 20.0)), std::max (0.0, std::ceil (high / 20.0) * 20.0), 20.0 };
    const auto y = [&] (double db) { return kTop + (axis.high - std::clamp (db, axis.low, axis.high)) / (axis.high - axis.low) * kPlotHeight; };

    auto svg = "<svg class=\"chart\" viewBox=\"0 0 " + at (kWidth) + " " + at (kHeight) + "\" role=\"img\" aria-label=\"" + escaped (title) + "\">\n"
             + yGrid (axis, y, "dBc");

    const auto band = kPlotWidth / (double) count;
    const auto width = std::min (24.0, (band - 12.0) / (hasBaseline ? 2.0 : 1.0) - 1.0);
    const auto floor = kTop + kPlotHeight;
    std::vector<std::string> labels;

    for (size_t i = 0; i < count; ++i)
    {
        const auto name = "H" + std::to_string (i + 2);
        const auto centre = kLeft + band * ((double) i + 0.5);
        const auto first = centre - (hasBaseline ? width + 1.0 : width / 2);
        labels.push_back (name);

        svg += "<text x=\"" + at (centre) + "\" y=\"" + at (floor + 16) + "\" text-anchor=\"middle\">" + name + "</text>\n";

        if (! curve.actual[i].is_number())
        {
            svg += "<text x=\"" + at (centre) + "\" y=\"" + at (floor - 6) + "\" text-anchor=\"middle\"><title>out of band</title>-</text>\n";
            continue;
        }

        if (hasBaseline && curve.baseline[i].is_number())
            svg += bar (first, width, floor, y (curve.baseline[i].get<double>()), "base", name + ", baseline: " + showValue (curve.baseline[i]) + " dBc");

        svg += bar (hasBaseline ? first + width + 2.0 : first, width, floor, y (curve.actual[i].get<double>()), "run",
                    name + ", this run: " + showValue (curve.actual[i]) + " dBc");
    }

    svg += "<line class=\"axis\" x1=\"" + at (kLeft) + "\" x2=\"" + at (kWidth - kRight) + "\" y1=\"" + at (floor) + "\" y2=\"" + at (floor) + "\"/>\n";

    return "<h3>" + escaped (title) + "</h3>\n" + legend (false, hasBaseline) + svg + "</svg>\n"
         + valuesTable (labels, curve.actual, hasBaseline ? curve.baseline : json(), "dBc");
}

std::string settings (const json& scenario)
{
    if (! scenario.contains ("config"))
        return {};

    // Copies: items() of a temporary would end before the loop runs.
    const auto& config = scenario["config"];
    const auto paramValues = config.value ("params", json::object());
    const auto signalValues = config.value ("signal", json::object());
    std::string params, signal;

    for (const auto& [name, value] : paramValues.items())
        params += (params.empty() ? "" : ", ") + name + " " + (value.is_object() ? "\"" + value.value ("text", "") + "\"" : showValue (value));

    for (const auto& [name, value] : signalValues.items())
        if (name != "type")
            signal += ", " + (value.is_array() ? std::to_string (value.size()) + " " + name
                                               : name + " " + (value.is_string() ? value.get<std::string>() : showValue (value)));

    std::string list;

    const auto row = [&] (const std::string& term, const std::string& description)
    {
        if (! description.empty())
            list += "<dt>" + term + "</dt><dd>" + escaped (description) + "</dd>";
    };

    row ("Signal", config.contains ("signal") ? config["signal"].value ("type", "") + signal : "");
    row ("Parameters", params.empty() ? "the plugin's defaults" : params);
    row ("Host", config.contains ("sample_rate") ? showValue (config["sample_rate"]) + " Hz, blocks of " + showValue (config.value ("block_size", json()))
                                                       + ", " + showValue (config.value ("channels", json()))
                                                       + (config.value ("channels", 0) == 1 ? " channel" : " channels")
                                                       + (config.value ("non_realtime", false) ? ", offline" : "")
                                                 : "");
    return "<dl>" + list + "</dl>\n";
}

std::string assertionsTable (const json& scenario)
{
    const auto assertions = scenario.value ("assertions", json::array());

    if (assertions.empty())
        return {};

    std::string rows;

    for (const auto& assertion : assertions)
    {
        const auto passed = assertion.at ("passed").get<bool>();
        rows += "<tr><td>" + escaped (assertion.at ("metric").get<std::string>()) + (assertion.value ("default", false) ? " <span class=\"muted\">(default)</span>" : "")
              + "</td><td>" + escaped (showRule (assertion.at ("rule"))) + "</td><td class=\"num\">" + showCell (assertion.at ("actual")) + "</td><td>"
              + statusBadge (passed ? "pass" : "fail") + "</td></tr>\n";
    }

    return "<h3>Rules</h3>\n<table><tr><th>Metric</th><th>Expected</th><th>This run</th><th>Result</th></tr>\n" + rows + "</table>\n";
}

// Every value of the scenario: against the baseline when there was a comparison, alone otherwise.
std::string valuesOf (const json& scenario)
{
    std::string rows;

    if (scenario.contains ("comparison"))
    {
        for (const auto& entry : scenario["comparison"])
        {
            const auto status = entry.at ("status").get<std::string>();
            const auto delta = entry.contains ("delta") ? showSigned (entry["delta"].get<double>()) + (entry.contains ("points_outside") ? " largest" : "") : "";

            rows += "<tr><td>" + escaped (entry.at ("metric").get<std::string>()) + "</td><td class=\"num\">" + showValue (entry.at ("channel")) + "</td><td class=\"num\">"
                  + showCell (entry.value ("baseline", json())) + "</td><td class=\"num\">" + showCell (entry.value ("actual", json())) + "</td><td class=\"num\">"
                  + delta + "</td><td class=\"num\">" + escaped (showTolerance (entry.at ("tolerance"))) + "</td><td>"
                  + (entry.value ("report_only", false) && status != "pass" ? "<span class=\"status st-info\">" + escaped (status) + " (for information)</span>"
                                                                           : statusBadge (status))
                  + "</td></tr>\n";
        }

        return rows.empty() ? "" : "<h3>Against the baseline</h3>\n<table><tr><th>Metric</th><th>Channel</th><th>Baseline</th><th>This run</th><th>Δ</th>"
                                   "<th>Tolerance</th><th>Status</th></tr>\n" + rows + "</table>\n";
    }

    const auto metrics = scenario.value ("metrics", json::object());

    for (const auto& [group, values] : metrics.items())
    {
        if (! values.contains ("per_channel"))
            continue;

        for (size_t c = 0; c < values["per_channel"].size(); ++c)
            for (const auto& [field, value] : values["per_channel"][c].items())
                rows += "<tr><td>" + escaped (group + "." + field) + "</td><td class=\"num\">" + std::to_string (c + 1) + "</td><td class=\"num\">" + showCell (value)
                      + "</td></tr>\n";
    }

    return rows.empty() ? "" : "<h3>Values</h3>\n<table><tr><th>Metric</th><th>Channel</th><th>This run</th></tr>\n" + rows + "</table>\n";
}

std::string cpuLine (const json& scenario)
{
    if (! scenario.contains ("metrics") || ! scenario["metrics"].contains ("cpu"))
        return {};

    const auto& cpu = scenario["metrics"]["cpu"];
    return "<p class=\"muted\">CPU: " + showValue (cpu.value ("core_percent", json())) + " % of one core (real-time factor "
         + showValue (cpu.value ("realtime_factor", json())) + "); 95 % of the blocks took under " + showValue (cpu.value ("p95_block_us", json()))
         + " µs. Shown for information, never compared.</p>\n";
}

std::string scenarioSection (const json& scenario)
{
    const auto id = scenario.at ("id").get<std::string>();
    auto section = "<section class=\"card\" id=\"" + anchor (id) + "\">\n<h2>" + escaped (id) + " " + statusBadge (scenario.at ("status").get<std::string>())
                 + "</h2>\n";

    if (scenario.contains ("description"))
        section += "<p class=\"muted\">" + escaped (scenario["description"].get<std::string>()) + "</p>\n";

    if (scenario.contains ("error"))
        section += "<p>" + escaped (scenario["error"].get<std::string>()) + "</p>\n";

    if (scenario.at ("status") == "removed")
        section += "<p>In the baseline, but no longer in the config.</p>\n";

    section += settings (scenario);

    if (const auto warnings = scenario.value ("warnings", json::array()); ! warnings.empty())
    {
        section += "<ul class=\"warnings\">";

        for (const auto& warning : warnings)
            section += "<li>" + statusBadge ("warning") + " " + escaped (warning.get<std::string>()) + "</li>";

        section += "</ul>\n";
    }

    section += assertionsTable (scenario);

    const auto freqs = scenario.contains ("metrics") && scenario["metrics"].contains ("frequency_response")
                           ? scenario["metrics"]["frequency_response"]["per_channel"][0].value ("freqs_hz", json())
                           : json();

    for (const auto& curve : curvesOf (scenario, "frequency_response", "magnitude_db"))
        section += responseChart (curve, freqs, "Frequency response, " + showChannels (curve.channels));

    for (const auto& curve : curvesOf (scenario, "harmonics", "h_dbc"))
        section += harmonicsChart (curve, "Harmonics, " + showChannels (curve.channels));

    return section + valuesOf (scenario) + cpuLine (scenario) + "</section>\n";
}
} // namespace

std::string formatHtml (const json& results, const ReportInfo& info)
{
    const auto& plugin = results.at ("plugin");
    const auto title = "Sonolint · " + plugin.at ("name").get<std::string>() + " " + plugin.at ("version").get<std::string>();
    const auto& summary = results.at ("summary");
    const auto& scenarios = results.at ("scenarios");

    std::string counts;

    for (const auto& status : statusesWorstFirst())
        if (const auto count = summary.value (status, 0); count > 0)
            counts += (counts.empty() ? "" : " · ") + std::to_string (count) + " " + statusBadge (status);

    std::string meta;

    for (const auto& part : { results.contains ("environment") ? results["environment"].value ("platform", "") : std::string(),
                              results.contains ("environment") ? results["environment"].value ("os", "") : std::string(),
                              results.contains ("environment") ? results["environment"].value ("cpu", "") : std::string(), showCommit (info) })
        if (! part.empty())
            meta += (meta.empty() ? "" : " · ") + escaped (part);

    std::string problems;

    for (const auto& scenario : scenarios)
        for (const auto& problem : problemsOf (scenario))
            problems += "<tr><td><a href=\"#" + anchor (scenario.at ("id").get<std::string>()) + "\">" + escaped (scenario.at ("id").get<std::string>()) + "</a></td><td>"
                      + escaped (problem.metric) + "</td><td>" + escaped (problem.text) + "</td><td>" + statusBadge (problem.status) + "</td></tr>\n";

    std::string index;

    for (const auto& scenario : scenarios)
        index += "<tr><td><a href=\"#" + anchor (scenario.at ("id").get<std::string>()) + "\">" + escaped (scenario.at ("id").get<std::string>()) + "</a></td><td>"
               + statusBadge (scenario.at ("status").get<std::string>()) + "</td></tr>\n";

    std::string sections;

    for (const auto& scenario : scenarios)
        sections += scenarioSection (scenario);

    return "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>"
         + escaped (title) + "</title>\n<style>\n" + kStyle + "</style>\n</head>\n<body>\n<main>\n<h1>" + escaped (title) + "</h1>\n<p class=\"meta\">" + meta
         + "</p>\n<p class=\"counts\">" + counts + "</p>\n<section class=\"card\">\n<h2>What didn't pass</h2>\n"
         + (problems.empty() ? "<p>No regressions, changes, failed rules or errors.</p>\n"
                             : "<table><tr><th>Scenario</th><th>Metric</th><th>What happened</th><th>Status</th></tr>\n" + problems + "</table>\n")
         + "<details><summary>All scenarios (" + std::to_string (scenarios.size()) + ")</summary>\n<table>\n" + index + "</table></details>\n</section>\n" + sections
         + "<footer>" + (results.contains ("sonolint_version") ? "Sonolint " + escaped (results["sonolint_version"].get<std::string>()) : std::string ("Sonolint"))
         + "</footer>\n</main>\n</body>\n</html>\n";
}
} // namespace sonolint
