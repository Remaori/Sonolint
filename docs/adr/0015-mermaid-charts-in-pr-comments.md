# 0015. `report.md` draws its charts with Mermaid `xychart-beta`

Status: accepted (2026-11-16). Result of a spike.

**Question.** GitHub renders Mermaid diagrams in Markdown. Does it also render the
newer `xychart-beta` diagram inside a pull request comment?

**What was tried.** A test comment on pull request #1 with one `xychart-beta` block:
a title, ten frequency labels on the x axis, a range in dB on the y axis and two
`line` series.

**Result.** GitHub drew it as a chart, with both lines, the axes and the title.

**Decision.** `report.md` includes an `xychart-beta` chart of the frequency response
for every scenario where it changed.

**What the chart can't do.**

- It has no legend and no dashed lines. The two series differ only by a colour that
  GitHub's theme picks, so the text next to the chart has to say which line is the
  baseline, and the table of values stays the place where a change is reported.
- The x axis holds labels, not a logarithmic scale: the points are evenly spaced.
  With octave or third-octave points that looks like a logarithmic axis anyway.
- GitHub lays its pan and zoom buttons over the bottom right corner, which hid the
  last label of the x axis in the test.
