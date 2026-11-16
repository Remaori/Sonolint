# 0013. Rendering needs JUCE initialised on the main thread, not a running message loop

Status: accepted (2026-11-15). Result of a spike.

**Question.** Can a console app render audio through a VST3 without a message loop
running, or does JUCE (or the plugin) need one?

**What was tried.** A throwaway program loaded Heft 0.5.0 (macOS arm64, JUCE 9.0.4,
Release), set a parameter and rendered 2 s of noise at 48 kHz in blocks of 512, in
three ways:

1. `juce::ScopedJuceInitialiser_GUI` in `main`, everything on the main thread, no
   message loop.
2. The same initialiser with the message loop running: the instance is created and
   destroyed on the message thread and the audio is processed on another thread, as
   a DAW does.
3. No JUCE initialisation at all.

Each one twice: with `juce::VST3PluginFormat` (module `juce_audio_processors`) and
with `juce::VST3PluginFormatHeadless` (module `juce_audio_processors_headless`).

**Result.** The six renders are byte-identical. The reported latency is 122 samples
in all of them, before and after the render.

**Decision.** `main` creates a `juce::ScopedJuceInitialiser_GUI`. The plugin instance
is created, prepared, processed and destroyed on the main thread, which acts as the
message thread. Sonolint never runs a message loop.

**Consequences.**

- Nothing posted to the message thread is ever delivered, so callbacks that JUCE
  sends asynchronously (a listener for latency or parameter changes) never arrive.
  Sonolint reads what it needs directly, e.g. `getLatencySamples()` after the warm-up.
- Way 3 worked with Heft but isn't relied on: JUCE then creates its message manager
  on first use and nothing promises that is enough for other plugins.

**Limits of the spike.** One plugin (Heft, itself built with JUCE) on macOS. On
Windows, Heft's own CI has rendered it this way with HeftMeasure, and Sonolint's CI
loads Heft there without a message loop.

**Open question: the headless module.** JUCE 9 can host plugins without its GUI
modules. In the spike `juce_audio_processors_headless` gave the same audio, compiled
in 17 s of CPU time instead of 97 s, and produced a 2.7 MB binary instead of 10.7 MB
(one architecture). `sonolint-host` links `juce_audio_processors` for now; switching
would be a decision of its own.
