#pragma once

#include "core/config/config.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace sonolint
{
// What makes `sonolint run` exit with 1 (--fail-on).
enum class FailOn
{
    regressions,   // errors, failed expect rules, and regressed or changed values
    assertions,    // errors and failed expect rules only
    none           // nothing
};

// Compares the results of runScenarios with `section`, this platform's scenarios in the
// baseline (null when there are none: then every value is new), channel by channel and
// field by field. `scenarios` and `tolerances` come from the config.
//
// Each scenario that ran without error gets a "comparison" list and a new status: the
// worst of its values and its expect rules. The baseline's scenarios that didn't run
// are added as "removed" (with filters, only those that match one). The summary is
// counted again.
void compareWithBaseline (nlohmann::json& results, const nlohmann::json& section, const std::vector<Scenario>& scenarios,
                          const std::map<std::string, Tolerance>& tolerances, const std::vector<std::string>& filters);

// Every status a scenario can end with, from worst to best.
const std::vector<std::string>& statusesWorstFirst();

// How many scenarios ended with each status.
nlohmann::json summarise (const nlohmann::json& scenarios);

// True when the results fail under this policy.
bool fails (const nlohmann::json& results, FailOn policy);
} // namespace sonolint
