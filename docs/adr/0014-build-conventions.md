# 0014. Build conventions

Status: accepted (2026-11-15)

Small decisions the build needed. Each takes the simplest option.

- **pocketfft is pinned by commit.** The project publishes no version tags, so
  `CMakeLists.txt` pins the commit at the head of its `cpp` branch (the header-only
  C++ version). Every other dependency is pinned by tag.
- **macOS deployment target 13.3.** It is the first macOS whose system C++ library
  supports all of C++20, including floating-point `std::to_chars` and `std::format`,
  so no code has to work around a missing library feature. GitHub's hosted macOS
  runners are newer. The binary doesn't run on older systems.
- **Warnings are errors in our own targets** (`COMPILE_WARNING_AS_ERROR`), with `/W4`
  on MSVC, `-Wall -Wextra -Wpedantic` on the core and JUCE's recommended flags on the
  host and the CLI. Third-party headers are included as system headers, so only our
  code is held to it.
- **The C++ runtime is linked statically on Windows**, as in Heft: `sonolint.exe`
  needs no Visual C++ redistributable.
- **The version is 0.0.x until the first release**, which is 0.1.0.
- **CI on Windows runs `build.ps1`**, which only calls
  `cmake --workflow --preset windows`. That keeps the shortcut tested.
