# 0005. Frequency response is measured with a stepped sine in v0.1

Status: accepted (2026-11-15)

**Decision.** v0.1 measures frequency response one sine at a time (third-octave
points by default).

**Alternative rejected.** An exponential sweep (Farina's method).

**Why.** A stepped sine is simpler, holds up with non-linear plugins and is enough at
third-octave resolution. The sweep, with phase and group delay at high resolution, is
planned for v0.3.
