#include "core/report/param_list.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace
{
// "\xE2\x88\x92" is the Unicode minus sign: three bytes, one character.
sonolint::PluginInfo examplePlugin()
{
    sonolint::PluginInfo plugin { "Example", "Acme", "1.2.3", {} };
    plugin.params.push_back ({ 0, "Gain", "100", 0.6000000238418579, "\xE2\x88\x92" "24 dB", "-6 dB", "12 dB", 0, false });
    plugin.params.push_back ({ 1, "Bypass", "101", 0.0, "Off", "Off", "On", 2, true });
    return plugin;
}
} // namespace

TEST_CASE ("list-params table lines up its columns by characters")
{
    CHECK (sonolint::formatParamTable (examplePlugin())
           == "Example 1.2.3 (Acme)\n"
              "\n"
              "#  Name    ID   Default  Text at 0  at 0.5  at 1   Steps       Bypass\n"
              "0  Gain    100  0.600    \xE2\x88\x92" "24 dB     -6 dB   12 dB  continuous\n"
              "1  Bypass  101  0.000    Off        Off     On     2           yes\n");
}

TEST_CASE ("list-params JSON carries every field and drops the float noise of the default")
{
    const auto json = nlohmann::json::parse (sonolint::formatParamJson (examplePlugin()));

    CHECK (json["name"] == "Example");
    CHECK (json["vendor"] == "Acme");
    CHECK (json["version"] == "1.2.3");
    REQUIRE (json["parameters"].size() == 2);

    const auto& gain = json["parameters"][0];
    CHECK (gain["index"] == 0);
    CHECK (gain["name"] == "Gain");
    CHECK (gain["id"] == "100");
    CHECK (gain["default"] == 0.6);
    CHECK (gain["text"]["0"] == "\xE2\x88\x92" "24 dB");
    CHECK (gain["text"]["0.5"] == "-6 dB");
    CHECK (gain["text"]["1"] == "12 dB");
    CHECK (gain["steps"] == 0);
    CHECK (gain["is_bypass"] == false);

    CHECK (json["parameters"][1]["steps"] == 2);
    CHECK (json["parameters"][1]["is_bypass"] == true);
}
