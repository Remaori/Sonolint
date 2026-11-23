#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace sonolint
{
// dB values below this are equal: numerical noise in a transparent plugin isn't a change.
inline constexpr double kFloorDb = -130.0;

// How a field is rounded in the baseline: dB, samples and degrees to 3 decimals, Hz to
// 2, linear values to 6 significant figures, and counts and yes/no as they are.
enum class Unit { db, samples, degrees, hz, linear, count, flag };

// lower: lower is better, so going down is "improved" and going up "regressed".
// target: any change beyond the tolerance is "changed".
// report: compared and shown, but never fails.
enum class Direction { lower, target, report };

struct FieldInfo
{
    std::string name;
    Unit unit;
    Direction direction;
    double tolerance = 0.0;   // the default `abs` tolerance, in the field's unit
    bool perPlugin = false;   // one value for the whole plugin, repeated in every channel
};

// Every metric group with its fields and their defaults (docs/METRICS.md).
const std::map<std::string, std::vector<FieldInfo>>& metricCatalog();

// A value, or every point of a curve, rounded the way the baseline stores it.
// Integers, yes/no and null stay as they are, and -0 becomes 0.
nlohmann::json roundForBaseline (const nlohmann::json& value, Unit unit);
} // namespace sonolint
