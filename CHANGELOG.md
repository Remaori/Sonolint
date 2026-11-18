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
  one.
- `sonolint run` reads [`sonolint.yml`](docs/CONFIG.md), renders the plugin for each
  scenario, measures it and writes `results.json`. It exits with 1 when a scenario
  fails one of its `expect` rules, gives NaN or infinity, or hangs.
- Three more groups of metrics: `determinism` (two renders must be bit-identical),
  `block_invariance` (the audio must not depend on how the host cuts it into blocks)
  and `cpu`. A latency reported wrong, a plugin that isn't deterministic and one that
  depends on the block size fail without any rule being written.
- `sonolint render` sends one signal through the plugin and writes the result to a WAV
  file, to listen to what was measured.
- **Baseline:** `sonolint baseline update` stores the accepted values of every scenario
  in `sonolint/baseline.json`, one section per platform. Commit it: an intentional
  change of sound then shows up in the pull request.
- `sonolint run` compares each value with the baseline, channel by channel, and reports
  it as `pass`, `new`, `improved`, `regressed`, `changed` or `removed`. A regression or
  a change fails the run; `--fail-on assertions` or `--fail-on none` relax that, and
  `--no-compare` skips the comparison.
- The `tolerances` of `sonolint.yml` now take effect.
- **Reports:** besides `results.json`, every run writes `report.md` (for the pull request
  comment, with a chart of each frequency response that changed), `report.html` (one page
  that opens without internet, light and dark, with the frequency response and the
  harmonics of each scenario against the baseline) and `junit.xml` (for test viewers).
  `--format` chooses which.
- The terminal shows a table of the scenarios that didn't pass, coloured in a terminal
  (set `NO_COLOR` to turn colour off).
- `--save-audio` writes the input and the output of every scenario as WAV files.
- `--artifact-url`, `--commit` and `--merged-into` fill the footer of the reports.
- Builds and tests with one command on Windows x64 (`cmake --workflow --preset windows`,
  or `.\build.ps1`) and on macOS as a universal binary (`cmake --workflow --preset macos`).
