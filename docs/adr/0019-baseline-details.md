# 0019. Details of the baseline and the comparison

Status: accepted (2026-11-17)

Decisions taken while writing the baseline and the comparison. `docs/METRICS.md` and
`docs/CONFIG.md` describe the resulting behaviour; this records why.

- **Each channel is stored and compared on its own.** The value reported for a metric
  is the highest across channels ([0018](0018-run-details.md)), so a channel that went
  down while a louder one stayed put would pass unseen. Comparing channel by channel
  catches it. The reported value is only for showing, so one regression doesn't
  appear twice. The cost is a baseline about twice as long for a stereo plugin.
- **Known limit: a swap of left and right goes unnoticed.** Both channels get the same
  signal, so swapped outputs give the same values. Different signals per channel are
  left for v0.2.
- **The phase is stored and compared, but never fails.** The specification says
  `report` metrics aren't stored, and also gives the phase a tolerance of 2°. The phase
  is deterministic, so storing it keeps the diffs stable. Its difference is wrapped to
  ±180° first, so a curve that crosses ±180° between two runs doesn't jump by 360°.
- **CPU is not stored.** It depends on the machine and its load, and would change in
  every diff. It stays in `results.json` and in the reports, without comparison. The
  warning for a CPU increase above 25 % in the specification is dropped, since there is
  nothing to compare against.
- **Fields without a tolerance in the specification** (`f0_hz`, `freqs_hz`,
  `components`, `skipped`, `reported_samples`) must be equal once rounded.
- **The new value is rounded like the stored one before comparing,** and a slack of
  1e-9 absorbs the rounding, so a run compares equal to the baseline it just wrote.
- **Tolerances are layered.** The field's default, then the top-level `tolerances`,
  then the scenario's own; each replaces only what it sets. Among keys that match a
  field, the exact path wins, then the longest glob. The order of keys in YAML can't
  decide: the parser doesn't keep it.
- **With `--filter`, the baseline's scenarios outside the filter are not `removed`:**
  they simply weren't asked for.
- **A scenario that ends in error keeps its stored values** on `baseline update`, and
  the command exits with 1 so the problem isn't missed.
- **A broken baseline file stops the run before anything is rendered,** with exit
  code 2, like a broken config.
- **`report_only` from the config behaves like the phase:** compared and shown, never
  failing. A field can't be taken out of `report` from the config.
