# Configuration: `sonolint.yml`

One YAML file, usually at the root of the plugin's repository, says which plugin to
test, with which signals and settings, and what to measure. `sonolint run` reads it.

The parser is strict: a key it doesn't know is an error, so a typo can't go unnoticed.
Every error names the exact place, for example:

```text
error: scenarios[3].signal.freq_hz: expected number, got "1k"
error: scenarios[0].metrcs: unknown key; did you mean "metrics"?
```

## Example

```yaml
version: 1

plugin:
  path: build/Heft_artefacts/Release/VST3/Heft.vst3   # --plugin replaces it

defaults:
  sample_rate: 48000
  block_size: 512        # largest block passed to the plugin
  channels: 2
  warmup_ms: 200
  tail_ms: 100
  non_realtime: false
  analysis: { skip_ms: 100 }

tolerances:              # for the comparison with a baseline
  level.gain_db: { abs: 0.05 }
  cpu.*: { report_only: true }

scenarios:
  - id: transparent-1k
    description: Fatness 0 must not change the sound
    params:
      Fatness: 0.0                 # normalized 0..1
      Output: { text: "0 dB" }     # in the plugin's own units
    signal: { type: sine, freq_hz: 1000, level_dbfs: -6, duration_s: 2 }
    metrics: [level, harmonics]
    expect:
      level.gain_db: { approx: 0.0, tol: 0.05 }
      harmonics.thdn_db: { max: -100 }

  - id: fat50-response
    params: { Fatness: 0.5 }
    signal: { type: stepped_sine, level_dbfs: -24, points: third_octave }
    metrics: [frequency_response]
    matrix:
      sample_rate: [44100, 48000, 96000]

  - id: robustness
    params: { Fatness: 0.7 }
    signal: { type: pink_noise, level_dbfs: -18, duration_s: 3, seed: 1 }
    metrics: [determinism, block_invariance, latency, cpu]
```

## Top level

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `version` | whole number | none | Required. Only `1` |
| `plugin.path` | path | none | The `.vst3` to test. Required unless `--plugin` is passed |
| `plugin.name` | text | none | Which plugin to load when the `.vst3` holds several |
| `defaults` | map | see below | Host settings for every scenario |
| `tolerances` | map | built-in | How far each metric may move from the baseline |
| `scenarios` | list | none | Required, at least one |

Paths written in the file are relative to the folder of the file. Paths on the
command line are relative to where you run the command.

## Host settings

They go in `defaults`, and any scenario can set its own.

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `sample_rate` | number | 48000 | In Hz |
| `block_size` | whole number | 512 | The largest block the plugin gets |
| `channels` | 1 or 2 | 2 | The same signal goes to every channel. The plugin must accept that layout on its main bus |
| `warmup_ms` | number | 200 | Silence processed and thrown away before the signal, so parameter smoothing settles |
| `tail_ms` | number | 100 | Silence processed after the signal |
| `non_realtime` | true or false | false | Tells the plugin it is rendering offline. Some plugins raise their quality then |
| `analysis.skip_ms` | number | 100 | Left out at the start of each measured stretch |
| `timeout_s` | number | 60 | A scenario that takes longer is reported as an error and the run stops |

Sidechains and extra buses are switched off: only the main bus is used.

## Scenarios

A scenario is one combination of parameters, signal and settings that is rendered
and measured, every time on a fresh instance of the plugin.

| Key | Type | Notes |
| --- | --- | --- |
| `id` | text | Required and unique. Lowercase letters, digits and hyphens. It names the scenario in the baseline, so don't rename it lightly |
| `description` | text | For people |
| `params` | map | Parameter values. See below |
| `state` | path | A binary state file, as a host would save it. It is applied before `params` |
| `signal` | map | Required. What goes into the plugin |
| `metrics` | list | Required. What to measure |
| `expect` | map | Checks on the measured values |
| `tolerances` | map | Like the top-level one, for this scenario only |
| `matrix` | map | Runs the scenario once per combination of values |

### `params`

The key is the name of the parameter as the plugin shows it, or its ID. If the name
isn't unique in the plugin, use the ID. `sonolint list-params --plugin <path>` prints
both. A name the plugin doesn't have stops the run before anything is rendered:

```text
error: scenario "fat50-1k": unknown parameter "Fatnes"; did you mean "Fatness"?
```

The value is either a number from 0 to 1 (the normalized value a host sends) or
`{ text: "..." }`, which the plugin converts from its own units. Parameters that
aren't listed keep the plugin's default. Sonolint never touches the bypass parameter
unless you set it.

### `signal`

`type` chooses the signal, and each type has its own keys:

| Type | Keys (default) |
| --- | --- |
| `sine` | `freq_hz`, `level_dbfs`, `duration_s` (2), `phase_deg` (0), `snap` (true) |
| `stepped_sine` | `points`: `third_octave`, `octave` or a list of Hz; `level_dbfs`; `step_s` |
| `impulse` | `amplitude` (0.5), `position_ms` (50), `duration_s` (1) |
| `white_noise`, `pink_noise` | `level_dbfs`, `duration_s`, `seed` (1) |
| `silence` | `duration_s` |
| `file` | `path` to a WAV file at the scenario's sample rate |

Keys without a default are required. [METRICS.md](METRICS.md#test-signals) describes
each signal exactly.

### `metrics`

A list of metric groups. Some need a particular signal:

| Group | Needs |
| --- | --- |
| `level`, `true_peak`, `determinism`, `block_invariance`, `cpu` | any signal |
| `harmonics`, `aliasing` | `sine`, long enough for the spectrum: more than 0.45 s at 48 kHz with the default `skip_ms` |
| `frequency_response` | `stepped_sine` |
| `latency` | `impulse`, `white_noise` or `pink_noise` |
| `silence` | `silence` |

[METRICS.md](METRICS.md) defines every group and its fields.

### `expect`

Rules on measured values that hold whatever the baseline says. The key is the path of
a field, `group.field`, of a group listed in `metrics`. Each rule is one of:

| Rule | Passes when the value is |
| --- | --- |
| `{ approx: x, tol: t }` | within `t` of `x` |
| `{ min: x }` | `x` or more |
| `{ max: x }` | `x` or less |
| `{ between: [a, b] }` | from `a` to `b` |
| `{ equals: n }` | exactly `n`, which can also be `true` or `false` |

For a field that is a curve, like `frequency_response.magnitude_db`, the rule applies
to every point. A scenario with a rule that doesn't pass ends as `assertion_failed`.

Three rules apply by themselves when their group is measured. Write your own rule for
the same field to replace one:

| Field | Rule | It catches |
| --- | --- | --- |
| `latency.mismatch_samples` | `{ between: [-1, 1] }` | a latency reported wrong |
| `determinism.bit_identical` | `{ equals: true }` | uninitialised memory, random state |
| `block_invariance.max_diff_dbfs` | `{ max: -120 }` | bugs in how blocks are handled |

### `tolerances`

How far a field may move from the baseline before it counts as a change. The key is
the path of a field and may use `*`; the value has `abs`, `rel` or both, and
`report_only: true` for a field that should be shown but never fail:

```yaml
tolerances:
  level.gain_db: { abs: 0.1 }
  harmonics.*: { abs: 2.0, rel: 0.05 }
  frequency_response.magnitude_db: { report_only: true }
```

A value passes when it is within `abs` + `rel` × |baseline| of the baseline. Each
field starts from its default ([METRICS.md](METRICS.md#metric-groups)); then the
top-level `tolerances` apply, then the scenario's own. Each one replaces only what it
sets, so `{ rel: 0.02 }` keeps the default `abs`. When several keys match a field, the
exact path wins, and otherwise the longest glob.

### `matrix`

Each key holds a list of values, and the scenario runs once per combination, up to 64.
The keys can be `sample_rate`, `block_size`, `channels`, `non_realtime` and
`params.<name>` (a normalized number).

Each combination gets its own id: the scenario's id, `@`, and the values in the
alphabetical order of the keys, with short names: `bs` for `block_size`, `ch` for
`channels`, `nrt` for `non_realtime`, `sr` for `sample_rate`, and a parameter under
its own name.

```yaml
- id: fat50
  matrix:
    sample_rate: [44100, 96000]
    block_size: [64, 512]
# fat50@bs=64,sr=44100   fat50@bs=64,sr=96000   fat50@bs=512,sr=44100   fat50@bs=512,sr=96000
```

## The baseline

The baseline holds the accepted values of every scenario, in `sonolint/baseline.json`
by default. Commit it next to `sonolint.yml`: an intentional change of sound then shows
up as a diff in the pull request. Sonolint never rewrites it during a normal run.

```text
sonolint baseline update [--config sonolint.yml] [--plugin PATH] [--baseline sonolint/baseline.json] [--filter GLOB]...
```

It runs the scenarios and writes their values as the baseline of the platform it runs
on (`windows-x64`, `macos-arm64` or `macos-x64`), leaving the other platforms alone.
With `--filter` it replaces only the scenarios that match and keeps the rest. A
scenario that ends in error keeps the values it had, and the command exits with 1.

[METRICS.md](METRICS.md#comparison-with-the-baseline) describes what is stored and
how each value is compared.

## Running

```text
sonolint run [--config sonolint.yml] [--plugin PATH] [--baseline sonolint/baseline.json] [--out sonolint-out]
             [--filter GLOB]... [--fail-on regressions|assertions|none] [--no-compare]
             [--format md,html,json,junit] [--save-audio] [--artifact-url URL] [--commit SHA] [--merged-into BRANCH]
             [--verbose]
```

`--filter` keeps the scenarios whose id matches the glob (`*` and `?`), and can be
repeated. Scenarios of the baseline outside the filter are left out of the comparison.

The terminal shows a table of the scenarios that didn't pass, one line per problem,
and the totals. Statuses are coloured when the output is a terminal and `NO_COLOR`
isn't set.

```text
SCENARIO    STATUS            WHAT
fat50-1k    changed           level.gain_db (channel 2): -0.8, was 0 (-0.8; tolerance 0.05)
robustness  assertion_failed  latency.mismatch_samples: is -10, expected from -1 to 1
3 scenarios: 1 changed, 1 assertion_failed, 1 pass
```

Without a baseline file, or without values for this platform in it, every value is
`new` and nothing fails. `--no-compare` skips the comparison altogether.

`--fail-on` decides what makes the run fail:

| `--fail-on` | Fails on |
| --- | --- |
| `regressions` (default) | errors, failed `expect` rules, and values that regressed or changed |
| `assertions` | errors and failed `expect` rules |
| `none` | nothing |

Improved and removed values never fail; accept them with `sonolint baseline update`.

### Reports

Every run writes these files to `--out`; `--format` chooses which (all four by
default). The GitHub Action needs `json`.

| File | `--format` | For | Contents |
| --- | --- | --- | --- |
| `results.json` | `json` | machines | Everything: the settings of each scenario, every metric per channel, the rules, the comparison, the machine and the times |
| `report.md` | `md` | the pull request comment and the job summary | The totals, a table of what didn't pass (worst first), a Mermaid chart of each frequency response that changed, every scenario in a folded block, and a footer |
| `report.html` | `html` | a closer look | One page that opens without internet, in a light and a dark theme: each scenario's settings, rules and values against the baseline, and its charts (frequency response, harmonics H2 to H10) |
| `junit.xml` | `junit` | test viewers of CI | A test suite per scenario and a test case per compared value or rule. Regressions, changes and failed rules are failures, errors are errors, anything removed is skipped |
| `audio/<id>.input.wav`, `audio/<id>.output.wav` | `--save-audio` | listening | The input and the raw output (with the plugin's latency and the tail) of each scenario, as 32-bit float |

`report.md` stays under 60 000 characters, so it fits in a GitHub comment: past that,
the folded block is cut first, then the charts; the table of what didn't pass never
is. In the charts and the HTML, channels whose values are within the tolerance of each
other are drawn once.

The footer of `report.md` and `report.html` takes what the run can't know: the link to
the full report (`--artifact-url`), the commit measured (`--commit`, a full SHA is
shortened) and the branch it was merged into before measuring (`--merged-into`), shown
as `Commit abc1234 (merged into main)`. Sonolint reads nothing from the CI itself; the
Action passes these.

| Exit code | Meaning |
| --- | --- |
| 0 | Nothing fails under `--fail-on` |
| 1 | Something fails under `--fail-on`, or a scenario timed out |
| 2 | The config, the baseline file or the command line is wrong |
| 3 | The plugin couldn't be loaded |
| 4 | A bug in Sonolint |
