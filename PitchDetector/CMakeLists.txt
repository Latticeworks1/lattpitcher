cmake_minimum_required(VERSION 3.22)
project(PitchDetector VERSION 1.0.0)

# Point to JUCE directory
add_subdirectory(../JUCE ../JUCE_build)

# Common source files
set(COMMON_SOURCES
    Source/PitchDetectionEngine.cpp
    Source/PitchDetectorGUI.cpp
    Source/AutotuneEngine.cpp
    Source/NeuralSpectralAutotuneEngine.cpp
)

# Common JUCE libraries
set(COMMON_JUCE_LIBS
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# =============================================================================
# Standalone Application
# =============================================================================
juce_add_gui_app(PitchDetectorApp
    PRODUCT_NAME "Pitch Detector"
    VERSION "1.0.0"
    COMPANY_NAME "Your Company"
    BUNDLE_ID "com.yourcompany.pitchdetector"
    ICON_BIG ""
    ICON_SMALL ""
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Pitch Detector requires microphone access to analyze audio input in real-time for pitch detection, recording, and musical note identification. Your audio data is processed locally and never transmitted."
)

target_sources(PitchDetectorApp PRIVATE
    Main.cpp
    Source/StandaloneApp.cpp
    Source/PitchDetectorProcessor.cpp
    Source/PitchDetectorEditor.cpp
    ${COMMON_SOURCES}
)

target_link_libraries(PitchDetectorApp PRIVATE
    juce::juce_audio_processors
    ${COMMON_JUCE_LIBS}
)

# =============================================================================
# Audio Plugin
# =============================================================================
juce_add_plugin(PitchDetectorPlugin
    PRODUCT_NAME "Pitch Detector"
    VERSION "1.0.0"
    COMPANY_NAME "Your Company"
    BUNDLE_ID "com.yourcompany.pitchdetector"
    PLUGIN_MANUFACTURER_CODE "YrCo"
    PLUGIN_CODE "PtDt"
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "/Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/VST3"
    AU_COPY_DIR "/Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/Components"
)

target_sources(PitchDetectorPlugin PRIVATE
    Source/PitchDetectorProcessor.cpp
    Source/PitchDetectorEditor.cpp
    ${COMMON_SOURCES}
)

target_link_libraries(PitchDetectorPlugin PRIVATE
    juce::juce_audio_processors
    ${COMMON_JUCE_LIBS}
)

# Preprocessor definitions for plugin
target_compile_definitions(PitchDetectorPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# =============================================================================
# Test: PV spectral envelope normalization
# =============================================================================
add_executable(PVTest
    tests/pv_test.cpp
    Source/AutotuneEngine.cpp
    Source/PitchDetectionEngine.cpp
)

target_include_directories(PVTest PRIVATE Source)
target_link_libraries(PVTest PRIVATE
    ${COMMON_JUCE_LIBS}
)
