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

For the comparison with a baseline. The key is the path of a field and may use `*`;
the value has `abs`, `rel` or both, or `report_only: true` for a field that should
never fail. They are read and checked now; the comparison itself arrives with the
baseline.

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

## Running

```text
sonolint run [--config sonolint.yml] [--plugin PATH] [--out sonolint-out] [--filter GLOB]... [--verbose]
```

`--filter` keeps the scenarios whose id matches the glob (`*` and `?`), and can be
repeated. The results go to `<out>/results.json`, and a summary of what didn't pass
to the terminal.

| Exit code | Meaning |
| --- | --- |
| 0 | Every scenario passed |
| 1 | A scenario failed an `expect` rule, gave NaN or infinity, or timed out |
| 2 | The config or the command line is wrong |
| 3 | The plugin couldn't be loaded |
| 4 | A bug in Sonolint |
