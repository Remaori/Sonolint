# 0007. All scenarios run in one process in v0.1

Status: accepted (2026-11-15)

**Decision.** `sonolint run` renders every scenario in the same process, one after
another.

**Alternative rejected.** One subprocess per scenario.

**Why.** It is much less to build. The cost is that a plugin that crashes takes the
whole run with it; the README documents that, and isolation (`--isolate`) is planned
for v0.3.
