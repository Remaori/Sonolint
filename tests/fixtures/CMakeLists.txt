# The test plugin: a minimal VST3 effect whose behaviour is known exactly, so what
# Sonolint measures on it can be checked. Built for the tests, never shipped.
# One plugin with five behaviours instead of five plugins: JUCE is compiled once
# (docs/adr/0020-one-test-plugin.md).

juce_add_plugin(SonolintFixture
    COMPANY_NAME                "Sonolint"
    PLUGIN_MANUFACTURER_CODE    Snlt
    PLUGIN_CODE                 Snf2
    FORMATS                     VST3
    PRODUCT_NAME                "SonolintFixture"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx
)

target_sources(SonolintFixture PRIVATE SonolintFixture.cpp)

target_compile_definitions(SonolintFixture
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SonolintFixture
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# The tests run it on this machine only: on a Mac, one architecture is enough and
# halves the build.
if(APPLE)
    set_target_properties(SonolintFixture SonolintFixture_VST3 PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

set_property(GLOBAL APPEND PROPERTY SONOLINT_FIXTURES SonolintFixture)
