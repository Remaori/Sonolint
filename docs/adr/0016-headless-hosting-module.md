# 0016. The host uses JUCE's headless hosting module

Status: accepted (2026-11-16). Follows from the spike in
[0013](0013-no-message-loop.md).

**Decision.** `sonolint-host` links `juce_audio_processors_headless` and loads plugins
with `juce::VST3PluginFormatHeadless`.

**Alternative rejected.** `juce_audio_processors` with `juce::VST3PluginFormat`, the
module the project started with. It adds the ability to show a plugin's editor and
brings in JUCE's GUI modules.

**Why.** Sonolint never shows a plugin's editor. In the spike the two formats rendered
byte-identical audio from Heft, and the headless program compiled in 17 s of CPU time
instead of 97 s and weighed 2.7 MB instead of 10.7 MB (one architecture). The binary
also stops carrying the image and text libraries bundled with JUCE's graphics code,
so `THIRD_PARTY_NOTICES.txt` is down to the VST 3 SDK and zlib on JUCE's side.

**Risk, and the first thing to check.** This was only tried with Heft, which is itself
built with JUCE. If a third-party plugin doesn't load in Sonolint, or behaves
differently from how it does in a DAW, check this decision before anything else:
build `sonolint-host` with `juce_audio_processors` and `juce::VST3PluginFormat` again
(the module in `CMakeLists.txt`, the include and the class in
`src/host/plugin_loader.cpp`) and compare.
