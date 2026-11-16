# 0006. v0.1 supports Windows x64 and macOS, VST3 only

Status: accepted (2026-11-15)

**Decision.** v0.1 builds, tests and ships on Windows x64 and on macOS (a universal
arm64 + x86_64 binary). On macOS it hosts VST3 only, not AU.

**Alternatives rejected.** Windows only, or adding Linux from the start.

**Why.** Day-to-day development happens on a Mac and Heft already builds there. Linux
waits for v0.3 so the scope doesn't grow before the first release. `sonolint-core`
is still built and tested on Linux in CI to keep it portable.

**Consequence.** Compilers, SIMD and FMA change the last decimals between platforms,
so the baseline keeps one section per platform (`windows-x64`, `macos-arm64`,
`macos-x64`).
