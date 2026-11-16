# 0008. The binary writes every report, charts included

Status: accepted (2026-11-15)

**Decision.** The `sonolint` executable generates the console summary, JSON,
Markdown, HTML and JUnit reports itself. Charts are SVG written by our own C++ code.

**Alternative rejected.** A Python or Node script with a charting library.

**Why.** Whoever uses the Action installs nothing else, and the HTML report opens
offline because it makes no external requests.
