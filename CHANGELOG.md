# Changelog

What changed in each version of Sonolint, newest first. Each version's section is also
the text of its release on GitHub, so it's written for people who use the tool.

## Unreleased

The first version, 0.1.0, is in development. So far:

- `sonolint list-params --plugin <path>` loads a VST3 the way a host would and lists
  its parameters: index, name, ID, default value, the text the plugin shows at 0, 0.5
  and 1, number of steps, and which one is the bypass. `--json` prints the same as JSON.
- `sonolint version` prints the version, commit, JUCE version and build date.
- The test signals (sine, stepped sine, impulse, white and pink noise, silence, WAV
  file) and the first seven groups of metrics (level, true peak, harmonics with THD and
  THD+N, aliasing, frequency response, latency, silence) are written and tested
  against cases with a known answer. [`docs/METRICS.md`](docs/METRICS.md) defines each
  one. They can't be used from the command line yet; that comes with `sonolint run`.
- Builds and tests with one command on Windows x64 (`cmake --workflow --preset windows`,
  or `.\build.ps1`) and on macOS as a universal binary (`cmake --workflow --preset macos`).
