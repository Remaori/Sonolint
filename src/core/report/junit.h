#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace sonolint
{
// junit.xml, for the test viewers of CI: a testsuite per scenario and a testcase per
// compared value (one per channel) or per expect rule. Regressions, changes and failed
// rules are failures, an error is an error, and anything removed is skipped, never a
// failure.
std::string formatJunit (const nlohmann::json& results);
} // namespace sonolint
