cmake_minimum_required(VERSION 3.22)
project(PitchDetector VERSION 1.0.0)

# Point to JUCE directory
add_subdirectory(../JUCE ../JUCE_build)

# Common source files
set(COMMON_SOURCES
    Source/PitchDetectionEngine.cpp
    Source/PitchDetectorGUI.cpp
    Source/AutotuneEngine.cpp
    Source/SimpleAutotuneGUI.cpp
    Source/PitchDisplayCard.cpp
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

# Option to redirect plugin copies to a local folder (useful in sandboxes/CI)
option(LOCAL_PLUGIN_COPY_DIR "Copy built plugins into local build folder" OFF)

if (LOCAL_PLUGIN_COPY_DIR)
    set(VST3_COPY_DIR "${CMAKE_BINARY_DIR}/LocalPlugins/VST3")
    set(AU_COPY_DIR   "${CMAKE_BINARY_DIR}/LocalPlugins/AU")
    file(MAKE_DIRECTORY "${VST3_COPY_DIR}")
    file(MAKE_DIRECTORY "${AU_COPY_DIR}")
else()
    # Default to user plugin locations
    set(VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    set(AU_COPY_DIR   "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
endif()
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
    VST3_COPY_DIR "${VST3_COPY_DIR}"
    AU_COPY_DIR   "${AU_COPY_DIR}"
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
# Test: PV spectral envelope normalization (experimental)
# =============================================================================
option(BUILD_PVTEST "Build the experimental PV test" OFF)
if (BUILD_PVTEST)
    add_executable(PVTest
        tests/pv_test.cpp
        Source/AutotuneEngine.cpp
        Source/PitchDetectionEngine.cpp
    )
    target_include_directories(PVTest PRIVATE Source)
    target_link_libraries(PVTest PRIVATE
        ${COMMON_JUCE_LIBS}
    )
endif()

# =============================================================================
# Test: LPC formant preservation
# =============================================================================
add_executable(LPCFormantTest
    test_lpc_formant.cpp
    Source/AutotuneEngine.cpp
    Source/PitchDetectionEngine.cpp
)
target_include_directories(LPCFormantTest PRIVATE Source)
target_link_libraries(LPCFormantTest PRIVATE
    ${COMMON_JUCE_LIBS}
)
# Enable CTest for simple validation
option(ENABLE_TESTS "Enable test targets and CTest integration" ON)
if (ENABLE_TESTS)
    include(CTest)
endif()

if (ENABLE_TESTS)
    add_test(NAME LPCFormantTest COMMAND LPCFormantTest)
endif()

# =============================================================================
# Packaging: zip local plugins into dist/
# =============================================================================
option(ENABLE_PACKAGING "Create distributable archives for plugins" ON)
if (ENABLE_PACKAGING)
    set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")
    file(MAKE_DIRECTORY "${DIST_DIR}")

    add_custom_target(package_plugins
        DEPENDS PitchDetectorPlugin_VST3 PitchDetectorPlugin_AU
        COMMAND ${CMAKE_COMMAND} -E echo "Packaging plugins to ${DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E tar "cfv" "${DIST_DIR}/pitch-detector-plugins-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}.zip" --format=zip
                "${CMAKE_BINARY_DIR}/LocalPlugins/VST3"
                "${CMAKE_BINARY_DIR}/LocalPlugins/AU"
        BYPRODUCTS "${DIST_DIR}/pitch-detector-plugins-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}.zip"
        COMMENT "Creating plugin archive"
    )
endif()
