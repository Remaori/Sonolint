# 0020. One test plugin with five behaviours

Status: accepted (2026-11-17). Replaces the five separate test plugins of the
specification (section 14) and the part of [0018](0018-run-details.md) about them.

**Decision.** The test plugin is one VST3, `SonolintFixture`, and its `Behaviour`
parameter chooses what it does: `Gain`, `Delay`, `Clipper`, `Filter` or `Buggy`. Each
behaviour does exactly what the plugin of the same name did, with the same parameters.

**Alternative rejected.** Five plugins, `SonolintGain` to `SonolintBuggy`.

**Why.** Every JUCE plugin compiles JUCE's modules itself. With five of them the
Windows CI job went from 6 to about 20 minutes, and every pull request waits for it.
With one, JUCE is compiled once for the tests. A compiler cache would also help, but
it is a new tool; it may come later.

**Rules that keep the five behaviours apart.**

- The host sets the parameters before `prepareToPlay`, and `prepareToPlay` fixes the
  behaviour and the latency it reports: the delay (plus 10 with `LieAboutLatency`) for
  `Delay`, the oversampling filters for `Clipper` at 4x, and 0 for the rest.
- The parameters of the other behaviours do nothing, and the four simulated bugs only
  act in `Buggy`. Each bug still fails exactly its own check in the integration tests.
