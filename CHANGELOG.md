# Changelog

What changed in each version of Sonolint, newest first. Each version's section is also
the text of its release on GitHub, so it's written for people who use the tool.

## Unreleased

The first version, 0.1.0, is in development. So far:

- `sonolint list-params --plugin <path>` loads a VST3 the way a host would and lists
  its parameters: index, name, ID, default value, the text the plugin shows at 0, 0.5
  and 1, number of steps, and which one is the bypass. `--json` prints the same as JSON.
- `sonolint version` prints the version, commit, JUCE version and build date.
- Builds and tests with one command on Windows x64 (`cmake --workflow --preset windows`,
  or `.\build.ps1`) and on macOS as a universal binary (`cmake --workflow --preset macos`).
