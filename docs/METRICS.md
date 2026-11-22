# Metrics

How Sonolint builds its test signals and what each metric means, with the formulas.
[CONFIG.md](CONFIG.md) says how to ask for them in `sonolint.yml`.

## Conventions

- **Full scale is 1.0.** dBFS = 20·log10(|x|). An RMS level in dBFS is 20·log10(rms),
  so a full-scale sine measures −3.01 dBFS RMS (there is no +3 dB as in AES17).
- **No value is ever −inf.** The argument of every logarithm is floored at 1e-15:
  an amplitude can't read below −300 dB and a ratio of powers below −150 dB.
- **Analysis runs in double precision.** The audio that goes into and out of the
  plugin is 32-bit float.
- **Analysis window.** Each measured stretch of a signal (a *segment*) is analysed
  without its first `analysis.skip_ms` (100 ms by default), which leaves out
  transients and parameter smoothing. A segment never includes the fade-out at the
  end of the signal.
- **Spectral analysis.** The FFT size N is the largest power of two that fits in the
  analysis window, between 2^14 and 2^18; a shorter window is a configuration error.
  The window is a 4-term Blackman-Harris. The power of a component is the sum of
  |X[k]|² over the bins k0 ± W with W = 6. Only ratios are reported, so the gain of
  the window cancels out.
- **Alignment.** The plugin's output is measured without its first samples, as many
  as the latency it reports, the way a DAW compensates it. Only `latency`,
  `determinism` and `block_invariance` look at the raw output.
- **Several channels.** Every metric is measured on each channel. The value reported
  is the worst channel, which is the highest number (for a yes/no field, "no"); for a
  curve, point by point. `results.json` also keeps each channel under `per_channel`.
- **NaN and infinity.** A single one anywhere in the output makes the scenario an
  `error`, whatever was asked.

## Test signals

Every signal is deterministic: the same configuration gives the same samples. They
are computed in double precision and converted to float at the end.

**`level_dbfs` is the peak level for `sine` and `stepped_sine`** (amplitude =
10^(L/20)) **and the RMS level for noise.**

| Type | Parameters (default) | Notes |
| --- | --- | --- |
| `sine` | `freq_hz`, `level_dbfs`, `duration_s` (2), `phase_deg` (0), `snap` (true) | When harmonics or aliasing are measured, the frequency moves to the nearest bin of the analysis FFT; the results report the real frequency |
| `stepped_sine` | `points`: `third_octave`, `octave` or a list in Hz; `level_dbfs`; `step_s` | One step per frequency, as one continuous wave. A step lasts 0.25 s or 50 cycles, whichever is longer, plus `skip_ms`. Points above 0.45·fs are left out |
| `impulse` | `amplitude` (0.5, linear), `position_ms` (50), `duration_s` (1) | A single sample |
| `white_noise`, `pink_noise` | `level_dbfs`, `duration_s`, `seed` (1) | Gaussian. The RMS is set exactly, before the fades |
| `silence` | `duration_s` | |
| `file` | `path` | A WAV file as it is. Its sample rate must be the scenario's: nothing is resampled |

- **Fades.** Sines, stepped sines and noise fade in and out over 5 ms with a raised
  cosine. Impulses, silence and files don't. In a stepped sine the fade-out has its
  own stretch after the last step.
- **`third_octave`** is the 31 nominal frequencies from 20 Hz to 20 kHz (20, 25,
  31.5, 40 … 16000, 20000) and **`octave`** the 10 from 31.5 Hz to 16 kHz.
- **Random numbers** come from xoshiro256\*\*, seeded with `seed` through SplitMix64.
  Gaussian values use the Box-Muller transform, two uniform values for each one.
  Nothing from `<random>` is used, because its distributions give different numbers
  on each standard library.
- **Pink noise** is Gaussian white noise multiplied by 1/√f in the frequency domain,
  the whole signal in one FFT, with nothing below 20 Hz.
- **Across machines.** The random integers are the same everywhere. Samples computed
  with `sin`, `cos` or `log` can differ in the last bit between platforms, which is
  one reason the baseline keeps a section per platform.

## Metric groups

Tolerances and directions are the defaults used when comparing against a baseline.
`lower` means lower is better, `target` means any change is a change, and `report`
means the value is only shown and never fails.

| Group | Fields | Needs | Default tolerance | Direction |
| --- | --- | --- | --- | --- |
| `level` | `peak_dbfs`, `rms_dbfs`, `gain_db`, `dc` | any signal | 0.1 dB, 0.05 dB, 0.05 dB, 1e-4 | target |
| `true_peak` | `true_peak_dbtp`, `sample_peak_dbfs` | any signal | 0.1 dB | target |
| `harmonics` | `f0_hz`, `thd_db`, `thdn_db`, `h_dbc[2..10]` | `sine` | 1.0 dB, 1.0 dB, 1.5 dB | lower |
| `aliasing` | `aliasing_dbc`, `components`, `skipped` | `sine` | 2.0 dB | lower |
| `frequency_response` | `freqs_hz[]`, `magnitude_db[]`, `phase_deg[]`, `gain_1k_db` | `stepped_sine` | 0.1 dB per point; phase 2° (informative) | target |
| `latency` | `reported_samples`, `measured_samples`, `mismatch_samples` | `impulse` or noise | 0, 0.5, 0.5 samples | target |
| `determinism` | `bit_identical`, `max_diff_dbfs` | any signal | exact | target |
| `block_invariance` | `max_diff_dbfs`, `rms_diff_dbfs` | any signal | 3 dB | lower |
| `cpu` | `realtime_factor`, `core_percent`, `p95_block_us` | any signal | none | report |
| `silence` | `noise_floor_dbfs`, `peak_dbfs` | `silence` | 1.0 dB | lower |

### level

Over the analysis window of the output: `peak_dbfs` is the largest |x| and `rms_dbfs`
its RMS. `gain_db` = rms_dbfs(output) − rms_dbfs(input), both over the same window.
`dc` is the mean of the output, linear.

### true_peak

`sample_peak_dbfs` is the largest sample. `true_peak_dbtp` is the largest value of
the wave rebuilt between the samples, as ITU-R BS.1770-4 (Annex 2) recommends: 4
points per sample at 44.1 and 48 kHz, 2 at 88.2 and 96 kHz, and the samples
themselves from 176.4 kHz.

The wave is rebuilt with a sinc under a Kaiser window (β = 8), 64 samples per point.
Each rebuilt point is within 0.002 dB of the real wave up to 0.45·fs. Like any
reading on a grid, the largest point can still fall short of the continuous peak: by
up to 0.17 dB for a tone at fs/4 and 0.55 dB at 0.45·fs with 4 points per sample.

### harmonics

Needs a sine. P₁ is the power of the fundamental and Pₖ the power of harmonic k. A
harmonic counts while k·f0 < min(20 kHz, fs/2 − W·Δf), with Δf = fs/N. P_res is the
power from 20 Hz to min(20 kHz, fs/2) without the bins of the fundamental.

$$\mathrm{THD_{dB}} = 10\log_{10}\frac{\sum_{k=2}^{10} P_k}{P_1} \qquad \mathrm{THD{+}N_{dB}} = 10\log_{10}\frac{P_{res}}{P_1}$$

THD+N is relative to the fundamental so it can be compared with THD. It differs from
the AES17 definition (relative to the total) by less than 0.05 dB when THD+N is below
−20 dB. `h_dbc[k]` = 10·log10(Pₖ / P₁), and `null` when the harmonic is out of band.

The fundamental has to be more than 2·W bins above 0; otherwise its bins and those of
its harmonics overlap, and the measurement is refused.

### aliasing

Needs a sine. For k = 2 to 25, a harmonic with k·f0 > fs/2 folds back to
f_a = (k·f0) mod fs, or fs − f_a if that is above fs/2. `aliasing_dbc` =
10·log10(Σ P_alias / P₁).

A folded harmonic is skipped when it lands within 2·W bins of the fundamental, of a
harmonic below fs/2, of DC or of fs/2: there its bins would overlap theirs and the
reading would be ambiguous. `components` and `skipped` count both kinds. A bin is
added once even if two harmonics fold onto it.

When more than half are skipped the frequency is a bad choice. At 12 kHz and 48 kHz
everything folds onto 0, 12 and 24 kHz. Use a frequency that doesn't divide the
sample rate, such as 9973 Hz.

### frequency_response

Needs a stepped sine. For each step, the window is the step without `skip_ms`, cut
down to a whole number of cycles (M samples). The complex amplitude at the step's
frequency f is taken from the input (X) and from the aligned output (Y):

$$A = \frac{2}{M}\sum_{n} x[n]\,e^{-j 2\pi f n / f_s} \qquad H = \frac{Y}{X}$$

`magnitude_db` = 20·log10|H|. `phase_deg` is the angle of H, unwrapped from one
frequency to the next. `gain_1k_db` is the magnitude at the point closest to 1 kHz.
Use a low level (−24 dBFS) with plugins that aren't linear.

### latency

`reported_samples` is what the plugin tells the host. `measured_samples` comes from
the cross-correlation of the raw output with the input, computed with the FFT: the
lag where its absolute value peaks, refined with a parabola through that point and
its two neighbours. `mismatch_samples` = measured − reported.

It needs an impulse or noise. A periodic signal such as a sine has no single answer.
A latency that is reported wrong makes a DAW comb-filter the plugin against the
tracks around it, so by default `mismatch_samples` must be between −1 and 1.

### determinism

The scenario is rendered twice, each time on a fresh instance. `bit_identical` says
whether the two outputs are the same bit for bit, and `max_diff_dbfs` is
20·log10(max|a − b|). By default `bit_identical` must be true. A difference means
uninitialised memory or random state inside the plugin.

### block_invariance

One render in blocks of `block_size` is compared with another in blocks of random
sizes from 1 to `block_size` (the sizes come from our generator with seed 12345, so
they are the same on every run). `max_diff_dbfs` is the largest difference and
`rms_diff_dbfs` its RMS. By default `max_diff_dbfs` must be −120 or less.

It catches bugs that only show up in some DAWs, because each one cuts the audio into
blocks its own way. A plugin that smooths its parameters once per block differs for a
legitimate reason: write your own rule for it in `expect`.

A plugin that isn't deterministic fails this check too, since no two of its renders
are equal.

### cpu

The time spent inside the plugin's processing call, block by block, on a steady clock.
The scenario is rendered 5 times and the figures are those of the median render:
`realtime_factor` is that time divided by the duration of the audio, `core_percent`
is 100 times that, and `p95_block_us` is the time in microseconds that 95 % of its
blocks stayed under. The warm-up isn't counted.

These values are only reported. The machines that run CI are too noisy to fail a
build over them.

### silence

With silence as the input: `noise_floor_dbfs` is the RMS of the output and `peak_dbfs`
its largest sample. They catch noise added by the plugin and self-oscillation.

## Comparison with the baseline

`sonolint baseline update` stores the values of each scenario, and `sonolint run`
compares the new ones with them field by field.

**What is stored.** Every field of every group except `cpu`, which depends on the
machine and its load. Each channel is stored on its own, in order: the value reported
for the worst channel is only for showing. Values are rounded: dB, samples and degrees
to 3 decimals, Hz to 2, linear values (`dc`) to 6 significant figures; counts and
yes/no as they are. Nothing that changes from run to run is stored: no times, no paths.

**How a value is compared**, channel by channel:

1. The new value is rounded the same way, so a run that matches the baseline compares
   equal.
2. dB values below −130 dB are raised to −130 dB on both sides: numerical noise in a
   transparent plugin isn't a change.
3. It passes when |new − baseline| ≤ `abs` + `rel` × |baseline|. A phase difference
   is first wrapped to ±180°, so 179° and −179° are 2° apart.
4. Otherwise, for a `lower` field it is `improved` if it went down and `regressed` if
   it went up; for a `target` field it is `changed`.
5. Curves go point by point and take the worst point; a different number of points is
   `changed`. Yes/no values and points without a value must be equal.
6. A value that isn't in the baseline is `new`; one in the baseline that isn't measured
   any more is `removed`. Renaming a scenario gives one of each.

`report` fields (`phase_deg`, and any field with `report_only` in the config) are
compared and shown, but never change the status of the scenario. `cpu` is shown in the
results and never compared.

**The status of a scenario** is the worst of its values and its `expect` rules, in this
order: `error` > `regressed` = `changed` > `assertion_failed` > `removed` > `improved` >
`new` > `pass`. A scenario of the baseline that no longer runs is `removed`. The reports
show `assertion_failed` as `rule failed`.

**Known limit.** Every channel gets the same signal, so a plugin that swaps left and
right gives the same values and the swap goes unnoticed. Different signals per
channel are planned for v0.2.
