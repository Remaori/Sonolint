# 0021. Details of the reports

Status: accepted (2026-11-17)

Decisions taken while writing the terminal table, `report.md`, `report.html` and
`junit.xml`. `docs/CONFIG.md` describes the result; this records why.

- **Every report is made from the results alone.** The comparison entries already
  carry the baseline's values, so the reports need nothing else and can be tested
  with fixed results.
- **Channels that coincide are drawn once.** Two channels coincide when every point,
  of this run and of the baseline, is within the tolerance of the comparison for that
  field, so numerical noise doesn't split one curve into two charts. The chart says
  which channels it stands for.
- **What didn't pass is one list for every report:** the worst status first and,
  within a status, the value farthest past its tolerance (|Δ| over the tolerance; a
  tolerance of 0, or a yes/no that flipped, counts as infinitely far), so the row to
  read first is on top. The terminal keeps one block per scenario, in the same order
  inside it.
- **Channels that moved alike share a row,** marked `(both channels)` or `(all N
  channels)`. They are alike when their rows would read the same, so nothing shown is
  lost; a channel that passed, or one that moved differently, keeps its own row.
- **The rows are for people:** `-51.5 dBc (was -79.8, +28.3; tolerance ±2)`. One
  decimal, or as many as the tolerance has, and more while the rounded change would
  look as if it were within the tolerance (`+0.104` over `0.1`, never `+0.1`); a
  failed rule gets more decimals while the rounded value would meet it. Whole values
  stay whole. `results.json` keeps every digit. The minus sign is ASCII, and the
  Windows console is switched to UTF-8 so `±` and plugin names show.
- **`report.md` uses the four columns of the HTML report** (scenario, metric, what
  happened, status): with seven, a comment on GitHub, about 800 pixels wide, cut the
  status off.
- **The `<details>` block of `report.md` has one row per scenario:** its status and
  what didn't pass. The values are in the HTML report; a row per value would fill the
  60 000 characters with a config of about 20 stereo scenarios.
- **The 60 000-character limit is counted in bytes,** which is safe since a character
  takes at least one byte. Past it the `<details>` rows go first, then the charts; the
  table of what didn't pass is never cut.
- **The commit comes from `--commit`, and `--merged-into` says it was merged.** The
  binary reads nothing from GitHub. In a pull request the Action checks out the merge
  of the pull request into its base branch, so the footer says
  `Commit abc1234 (merged into main)`: the commit is the head of the pull request, and
  what was measured is its merge. A 40-character SHA is shortened to 7.
- **Charts use the first two slots of a palette checked for colour blindness** in the
  light and the dark theme; this run is solid and the baseline dashed (a hatched,
  dashed outline for bars), so colour is never the only difference. Statuses always
  carry their word.
- **The frequency response marks the points that moved,** but only when they are
  fewer than half: when most of the curve moved, the two lines say it.
- **Harmonic bars stand on a fixed floor of -120 dBc or lower,** so their height shows
  how strong a harmonic is. When every harmonic is below -130 dBc, the floor of the
  comparison, the chart is replaced by a sentence.
- **No scripts in the HTML.** Hovering shows each value through SVG titles, and every
  chart has its values in a folded table, so nothing depends on hovering.
- **`junit.xml`:** a removed scenario or value is `skipped`, never a failure; a
  `report_only` change passes; a scenario with nothing checked gets one passing case,
  since a suite needs one.
- **`--save-audio` writes the raw output,** with the plugin's latency and the tail,
  next to the input, both as 32-bit float.
- **Without `json` in `--format`, `results.json` isn't written,** except when a scenario
  times out, where it is the only report there is. The Action must include `json`:
  its outputs come from that file.
- **Colour in the terminal** needs a terminal and no `NO_COLOR`. On Windows, Sonolint
  also turns on the console's handling of ANSI codes, which older consoles need.
