#pragma once

namespace sonolint
{
// True when standard output is a terminal that shows ANSI colours. On Windows this also
// turns on the console's handling of those codes, which older consoles need.
bool stdoutIsColourTerminal();
} // namespace sonolint
