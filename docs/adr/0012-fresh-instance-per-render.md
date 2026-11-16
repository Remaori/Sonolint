# 0012. Every render uses a fresh plugin instance

Status: accepted (2026-11-15)

**Decision.** Each render creates a new instance of the plugin and destroys it when
it finishes.

**Alternative rejected.** Reusing one instance for several renders.

**Why.** No scenario can leave state behind for the next one, so results don't depend
on the order of the scenarios. It is slower with heavy plugins; that is the price.
