cmake_minimum_required(VERSION 3.22)
project(Harmonizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE
add_subdirectory(libs/JUCE)

# Rubber Band Library — single-file compilation (bundles KissFFT + Speex resampler)
add_library(rubberband STATIC
    libs/rubberband/single/RubberBandSingle.cpp
)
target_include_directories(rubberband PUBLIC libs/rubberband)
target_compile_definitions(rubberband PRIVATE
    KISSFFT_DATATYPE=float
)

# Plugin target
juce_add_plugin(Harmonizer
    COMPANY_NAME "Harmonizer"
    PLUGIN_MANUFACTURER_CODE Harm
    PLUGIN_CODE Hrm1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Harmonizer"
    NEEDS_MIDI_INPUT TRUE
    IS_SYNTH FALSE
)

target_sources(Harmonizer PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PitchDetector.cpp
    src/MidiNoteTracker.cpp
    src/HarmonyVoice.cpp
    src/VoicePool.cpp
    src/PanningEngine.cpp
)

target_include_directories(Harmonizer PRIVATE src)

target_link_libraries(Harmonizer
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        rubberband
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(Harmonizer PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)
