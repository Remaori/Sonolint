# Test plugins: minimal VST3 effects whose behaviour is known exactly, so what
# Sonolint measures on them can be checked. Built for the tests, never shipped.

function(sonolint_add_fixture name code)
    juce_add_plugin(${name}
        COMPANY_NAME                "Sonolint"
        PLUGIN_MANUFACTURER_CODE    Snlt
        PLUGIN_CODE                 ${code}
        FORMATS                     VST3
        PRODUCT_NAME                "${name}"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             Fx
    )

    target_sources(${name} PRIVATE ${name}.cpp)

    target_compile_definitions(${name}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )

    target_link_libraries(${name}
        PRIVATE
            juce::juce_audio_processors
            ${ARGN}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    # The tests run them on this machine only: on a Mac, one architecture is enough
    # and halves the build.
    if(APPLE)
        set_target_properties(${name} ${name}_VST3 PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    set_property(GLOBAL APPEND PROPERTY SONOLINT_FIXTURES ${name})
endfunction()

sonolint_add_fixture(SonolintGain    Sng1)
sonolint_add_fixture(SonolintDelay   Snd1)
sonolint_add_fixture(SonolintClipper Snc1 juce::juce_dsp)
sonolint_add_fixture(SonolintFilter  Snf1)
sonolint_add_fixture(SonolintBuggy   Snb1)
