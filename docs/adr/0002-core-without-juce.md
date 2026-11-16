# 0002. The core has no JUCE and renders through the `Renderer` interface

Status: accepted (2026-11-15)

**Decision.** `sonolint-core` (configuration, signals, metrics, comparison, reports)
never includes JUCE. It asks for audio through the `Renderer` interface, which
`sonolint-host` implements with JUCE.

**Alternative rejected.** Using JUCE everywhere.

**Why.** The core is tested in seconds with a fake renderer, without building any
plugin. It stays portable to Linux, and the split keeps "what to measure" apart from
"how to run a plugin".

**How it is enforced.** `sonolint-core` links no `juce::` target, and the `core-linux`
CI job builds it with `-DSONOLINT_CORE_ONLY=ON`, which never downloads JUCE. A JUCE
header included from `src/core/` fails that job.
