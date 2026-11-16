# 0003. Configuration in YAML; results and baseline in JSON

Status: accepted (2026-11-15)

**Decision.** People write `sonolint.yml`. Sonolint writes `results.json` and
`baseline.json`.

**Alternative rejected.** JSON for everything.

**Why.** YAML is easier to write by hand and is what GitHub Actions users already
know. JSON is better for machines and gives stable, reviewable diffs.
