#pragma once

#include <string>

namespace sonolint
{
// The platform this binary was built for. Results differ in the last decimals between
// platforms (compiler, SIMD, FMA), so they are kept apart by this name.
inline std::string currentPlatform()
{
   #if defined (_WIN32)
    return "windows-x64";
   #elif defined (__APPLE__) && defined (__aarch64__)
    return "macos-arm64";
   #elif defined (__APPLE__)
    return "macos-x64";
   #else
    return "linux";
   #endif
}
} // namespace sonolint
