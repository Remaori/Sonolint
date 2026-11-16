# 0009. The GitHub Action is a composite action in PowerShell

Status: accepted (2026-11-15)

**Decision.** `action.yml` is a composite action whose steps run in PowerShell
(`pwsh`) and call `gh`.

**Alternative rejected.** A JavaScript action.

**Why.** There is no `node_modules` folder or bundle to maintain, and `pwsh` and `gh`
come preinstalled on GitHub's Windows and macOS runners.
