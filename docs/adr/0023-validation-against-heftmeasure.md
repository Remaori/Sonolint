# 0023. Validation against HeftMeasure

Status: accepted (2026-11-18)

**What was compared.** Sonolint and HeftMeasure, the tool Sonolint grew out of, on the
same build of Heft 0.5.0 (macOS arm64) with the same settings: the latency of the wet,
dry and clipper paths; THD+N, H2 and H3 of a 1 kHz sine at −6 dBFS for Fatness 25, 50
and 100 % and Color 0, 50 and 100 %; and the sample and true peaks of the clipper at
Fatness 50 % and Output +12 dB, with a 200 Hz sine and a 12 kHz sine at 45°, both at
−0.9 dBFS. HeftMeasure prints one decimal.

**Distortion: they agree within 0.05 dB.**

| Fatness, Color | THD+N, Sonolint | THD+N, HeftMeasure | H2 | H3 |
| --- | --- | --- | --- | --- |
| 25 %, 0 % | −31.71 | −31.7 | floor, both | −31.71 / −31.7 |
| 25 %, 50 % | −26.00 | −26.0 | −27.12 / −27.1 | −32.47 / −32.5 |
| 25 %, 100 % | −22.35 | −22.3 | −22.62 / −22.6 | −34.77 / −34.8 |
| 50 %, 0 % | −24.14 | −24.1 | floor, both | −24.16 / −24.2 |
| 50 %, 50 % | −21.77 | −21.8 | −24.56 / −24.6 | −25.11 / −25.1 |
| 50 %, 100 % | −19.66 | −19.7 | −20.58 / −20.6 | −27.24 / −27.2 |
| 100 %, 0 % | −24.46 | −24.5 | floor, both | −24.47 / −24.5 |
| 100 %, 50 % | −21.99 | −22.0 | −24.66 / −24.7 | −25.46 / −25.5 |
| 100 %, 100 % | −19.79 | −19.8 | −20.64 / −20.6 | −27.66 / −27.7 |

The tolerance of THD+N is 1.0 dB and of the harmonics 1.5 dB.

**Peaks: they agree within 0.001 dB.** Sonolint measures −0.001 and +0.000 dBTP and
dBFS; HeftMeasure prints −0.00 and +0.00 for both. The clipper holds the output at full
scale, so this checks the two at that point only.

**Latency: they agree, once both measure the same thing.** Heft reports 122 samples.

| Path | Sonolint, impulse | Sonolint, pink noise | HeftMeasure, impulse peak | HeftMeasure, phase at 1 kHz |
| --- | --- | --- | --- | --- |
| wet | 121.03 | 121.37 | 121 | 121.96 |
| dry (Mix 0 %) | 121.03 | 121.37 | 121 | 121.99 |
| clipper on | 121.03 | 121.37 | 121 | 121.96 |

Sonolint finds the peak of the cross-correlation of output and input, as section 9 of
the specification defines it; with an impulse that is the peak of the impulse response,
the same as HeftMeasure's first figure. HeftMeasure's own check uses the phase delay at
1 kHz, a different quantity. They differ for Heft because its latency is made whole
with a fractional-delay allpass filter: its delay is exact at low frequencies, and its
impulse response peaks one sample early. Noise, with more energy at low frequencies,
lands in between.

**Decision.** Neither tool is wrong and nothing changes. Sonolint keeps the definition
of the specification. Measuring the group delay from the phase instead would match
HeftMeasure's 1 kHz figure, but it would also report the group delay of ordinary
filters (a low-pass at 1 kHz delays low frequencies by several samples) as a latency
that was never reported, which is worse. For Heft, the default rule on the mismatch
(±1 sample) passes with an impulse at −0.97 and with noise at −0.63; the example config
measures latency with noise, to keep a margin across platforms.
