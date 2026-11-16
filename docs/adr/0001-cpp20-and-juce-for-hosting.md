# 0001. C++20 and JUCE 9.0.4 host the plugin

Status: accepted (2026-11-15)

**Decision.** Sonolint is written in C++20 and loads the VST3 with JUCE 9.0.4.

**Alternative rejected.** Python with DawDreamer or pedalboard.

**Why.** It is the same stack as Heft, whose `HeftMeasure` tool is the seed of this
project, so its hosting code carries over. Writing the host in C++ is also part of
what the project sets out to show.
