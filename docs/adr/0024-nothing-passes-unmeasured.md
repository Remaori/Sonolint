# 0024. Nothing passes unmeasured

Status: accepted (2026-11-23)

A review of the code before 0.1.0 found four ways in which a scenario could pass, or a
whole run could be lost, without measuring what its config says.

- **A rule has to hold on every channel.** It was evaluated on the value reported for
  all of them, which is the highest one ([ADR 0018](0018-run-details.md)), so a channel
  6 dB lower met `0 ± 0.1`. A broken rule now shows the channel farthest from meeting
  it and, when only some break it, which ones. The comparison with the baseline always
  went channel by channel. A test had the old behaviour as expected; it was wrong.
- **What the plugin understood from a text is read back.** Plugins read whatever they
  can: `Cliper` became the first choice of the list and the scenario measured that. In a
  list of choices a text it doesn't have is an error, like any other typo in the config.
  Elsewhere it is a warning, because the same value can be written in many ways (`1 kHz`
  and `1000 Hz`); two texts that start with the same number count as equal, whatever the
  unit, to within the rounding of what the plugin shows.
- **A state that can't be read is an error.** JUCE ignores it, and the scenario measured
  the plugin's defaults. Only the format of JUCE's own hosts can be read; `.vstpreset`
  is left for later. No test loads a valid state yet: that needs a file written by a
  JUCE host.
- **A layout the plugin refuses ends its scenario, not the run.** It was reported like a
  plugin that can't be loaded, which stops everything with exit code 3 and no report,
  although the other scenarios may well run.
- **`level`, `true_peak` and `silence` take every step of a stepped sine.** They took the
  first one, so a filter that falls 21 dB read -0.04 dB. Refusing the combination was the
  alternative; measuring all of it is what someone who writes it expects.

None of this changes the values of a scenario that was measured right: Heft's 23 give the
same ones.
