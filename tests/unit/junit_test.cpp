#include "core/report/junit.h"
#include "report_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

TEST_CASE ("junit.xml is byte for byte the golden one")
{
    std::ifstream golden (std::filesystem::path (SONOLINT_GOLDEN) / "junit.xml", std::ios::binary);
    REQUIRE (golden);
    std::ostringstream expected;
    expected << golden.rdbuf();

    CHECK (sonolint::formatJunit (test::reportResults()) == expected.str());
}

TEST_CASE ("junit.xml: removed is skipped, never a failure, and text is escaped")
{
    const json results { { "scenarios", json::array ({ { { "id", "quiet" }, { "status", "removed" } },
                                                       { { "id", "x" }, { "status", "error" }, { "error", "a < b & \"c\"" } } }) } };
    const auto text = sonolint::formatJunit (results);

    CHECK (text.find ("<testsuites name=\"Sonolint\" tests=\"2\" failures=\"0\" errors=\"1\" skipped=\"1\">") != std::string::npos);
    CHECK (text.find ("<skipped message=\"in the baseline, but no longer in the config\"/>") != std::string::npos);
    CHECK (text.find ("<error message=\"a &lt; b &amp; &quot;c&quot;\"/>") != std::string::npos);
}
