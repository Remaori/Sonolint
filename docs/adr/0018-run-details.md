# 0018. Details of `sonolint run` the spec left open

Status: accepted (2026-11-16)

Decisions taken while writing the config parser, the runner, the host and the test
plugins. `docs/CONFIG.md` and `docs/METRICS.md` have the resulting behaviour; this
records why.

- **The value of a metric over several channels is the highest one.** "The worst
  channel" is clear for metrics where lower is better; using the maximum for every
  field keeps one rule. Each channel's own values stay in `results.json` under
  `per_channel`. Known weakness: a change in the channel with the lower value of a
  `target` metric doesn't show in the reported value. Whether the baseline should
  keep the channels apart is decided with the baseline. The `expect` rules looked at
  that value too until [ADR 0024](0024-nothing-passes-unmeasured.md).
- **`plugin.name` is a config key.** A `.vst3` can hold several plugins and something
  has to say which one; the strict parser needs the key to be known.
- **`defaults.timeout_s` sets the time limit of a scenario** (60 s). A render that
  hangs is stuck inside the plugin, so a watchdog on another thread writes the results
  so far, marks that scenario as an error and ends the process with exit code 1.
- **`sonolint render` arrives now.** It is a few lines on top of the host and it is
  how a test plugin is listened to when a measurement looks wrong.
- **Matrix ids use short names**: `bs`, `ch`, `nrt`, `sr`, and a parameter under its
  own name, in the alphabetical order of the full keys. The value is written as it
  is in the config.
- **The random block sizes always use seed 12345.** There is no key for it until
  someone needs one.
- **Typos get a suggestion everywhere**, not only in parameter names: unknown keys,
  metric names and signal types too.
- **A scenario's failure doesn't stop the run**, except a plugin that can't be
  loaded, which no later scenario could survive either.
- **`RenderResult` has a list of warnings**, for things like a latency that changes
  during the render.
- **Results are built as JSON from the start**, not as a second set of structs that
  mirror it. The `expect` rules and, later, the baseline address fields by path, which
  JSON already does.
- **The test plugins are built for one architecture on a Mac**, the one that runs
  the tests, which halves their build. Each one still compiles JUCE's modules itself,
  as `juce_add_plugin` sets a plugin up, and that is most of the build time they add.
- **A plugin that isn't deterministic also fails block invariance**, because no two
  of its renders are equal. Its test scenario therefore asks for determinism only.
- **The checks on the test plugins run the real binary.** A small program executes
  `sonolint`, then reads `results.json`. The integration cases planned for the
  baseline will use the same program.
