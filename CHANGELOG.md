# Changelog

What changed in each version of Sonolint, newest first. Each version's section is also
the text of its release on GitHub, so it's written for people who use the tool.

## Unreleased

Nothing yet.

## 0.1.0

The first version. Sonolint runs a compiled VST3 effect like a DAW would, measures how
it sounds and tells you in every pull request whether the sound changed. It runs on
Windows x64 and macOS.

- **GitHub Action:** `uses: Remaori/Sonolint@v0` downloads Sonolint, measures your
  plugin, comments the report on the pull request (one comment per plugin and platform,
  edited on every push) and fails the job when something regressed. `mode: baseline` makes the baseline
  on the same kind of runner the comparison uses. See [`docs/ACTION.md`](docs/ACTION.md).
- **`sonolint run`** reads [`sonolint.yml`](docs/CONFIG.md), renders the plugin for each
  scenario on a fresh instance, measures it and compares every value with the baseline,
  channel by channel: `pass`, `new`, `improved`, `regressed`, `changed` or `removed`. It
  exits with 1 when something regressed or changed, a rule in `expect` failed, the
  output had NaN or infinity, or a scenario hung; `--fail-on` relaxes that.
- **Ten groups of metrics**, each defined in [`docs/METRICS.md`](docs/METRICS.md): level,
  true peak, harmonics with THD and THD+N, aliasing, frequency response, latency,
  determinism, block invariance, CPU and silence. A latency reported wrong, a plugin that
  isn't deterministic and one that depends on the block size fail without any rule.
- **Test signals:** sine, stepped sine, impulse, white and pink noise, silence and WAV
  files, the same on every run.
- **Baseline:** `sonolint baseline update` stores the accepted values in
  `sonolint/baseline.json`, one section per platform. Commit it: an intentional change of
  sound then shows up in the pull request.
- **Reports:** `results.json`, `report.md` for the pull request comment (with a chart of
  each frequency response that changed), `report.html` (one page that opens without
  internet, light and dark, with the frequency response and the harmonics against the
  baseline) and `junit.xml`. The terminal shows a table of what didn't pass.
- **Other commands:** `sonolint list-params` lists a plugin's parameters, `sonolint
  render` sends one signal through it to a WAV file, and `--save-audio` keeps the audio
  of every scenario.
- Builds and tests with one command on Windows x64 (`cmake --workflow --preset windows`,
  or `.\build.ps1`) and on macOS as a universal binary (`cmake --workflow --preset macos`).
