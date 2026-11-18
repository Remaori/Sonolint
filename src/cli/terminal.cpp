#include "cli/terminal.h"

#include <cstdio>

#ifdef _WIN32
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <io.h>
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace sonolint
{
bool stdoutIsColourTerminal()
{
   #ifdef _WIN32
    if (_isatty (_fileno (stdout)) == 0)
        return false;

    const auto console = GetStdHandle (STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return GetConsoleMode (console, &mode) != 0 && SetConsoleMode (console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
   #else
    return isatty (fileno (stdout)) != 0;
   #endif
}
} // namespace sonolint
