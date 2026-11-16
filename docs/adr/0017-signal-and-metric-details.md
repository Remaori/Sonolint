# 0017. Signal and metric details the spec left open

Status: accepted (2026-11-16)

Decisions taken while writing the generators and the first seven metric groups.
`docs/METRICS.md` has the resulting definitions; this records why.

- **Same samples everywhere, as far as the platform allows.** The random integers
  are checked against exact golden values. Anything computed with `sin`, `cos` or
  `log` is checked with a small tolerance, because each platform's math library may
  round the last bit differently. The alternative, our own versions of those
  functions, is a lot of code to maintain for no gain: the baseline already keeps a
  section per platform.
- **Random numbers.** xoshiro256\*\* is seeded through SplitMix64, as its authors
  recommend. Box-Muller uses two uniform values and returns one Gaussian value.
- **Segments leave the fade-out outside.** A sine's RMS over a window that included
  its 5 ms fade-out would read 0.007 dB low. In a stepped sine the wave is continuous
  from step to step, and the fade-out gets its own stretch after the last one.
- **Noise has exactly the requested RMS.** It is scaled after it is generated, so
  `level_dbfs` doesn't depend on the luck of the seed.
- **Stepped sine points.** `third_octave` and `octave` use the nominal frequencies
  people expect to read (1250 Hz, not 1258.9). Points above 0.45·fs aren't generated,
  since they aren't measured.
- **True peak uses 64 samples per point, not 12.** The spec asks for at least 12 and
  for a ripple of 0.1 dB at most up to 0.45·fs. A sinc of 12 samples under a Kaiser
  window is off by 2 dB or more at 0.45·fs; with 64 the error stays under 0.002 dB.
- **A folded harmonic is skipped within 2·W bins of other content, not W.** Each
  component is summed over ±W bins, so two components closer than 2·W would share
  bins and the aliasing figure would include part of a real harmonic.
- **`gain_1k_db` is the point closest to 1 kHz**, for lists of points without it.
- **A silent output measures a latency of 0** for now. It should be reported as
  impossible to measure once scenarios have statuses.
- **Which tests arrive with this milestone.** The rows of the test table for
  signals and for these seven groups. The rows for determinism, configuration,
  matrices, comparison and golden files come with the code they test.
