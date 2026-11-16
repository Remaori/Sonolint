# 0004. The baseline is versioned in the plugin's repository

Status: accepted (2026-11-15)

**Decision.** The accepted metric values live in `sonolint/baseline.json`, committed
to the repository of the plugin under test. Sonolint never rewrites it in CI.

**Alternative rejected.** Keeping the baseline as a build artifact of `main`.

**Why.** An intentional change of sound shows up as a diff in the pull request, where
it gets reviewed, and no extra infrastructure is needed.
