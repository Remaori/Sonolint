#pragma once

namespace sonolint
{
// True when standard output is a terminal that shows ANSI colours. On Windows this also
// turns on the console's handling of those codes, which older consoles need.
bool stdoutIsColourTerminal();

// On Windows, makes the console show the UTF-8 that Sonolint prints: "±" in the reports,
// and names of plugins and parameters in any language. Elsewhere it does nothing.
void useUtf8Console();
} // namespace sonolint
