cmake_minimum_required(VERSION 3.22)

project(YINPitchDetector VERSION 1.0.0)

# Add JUCE
add_subdirectory(../JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

# Create the app
juce_add_gui_app(YINPitchDetectorApp
    PRODUCT_NAME "YIN Pitch Detector"
    BUNDLE_ID com.yourcompany.yinpitchdetector
    ICON_BIG ""
    MICROPHONE_PERMISSION_ENABLED TRUE)

# Source files
target_sources(YINPitchDetectorApp PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/YINPitchDetector.h
    Source/YINPitchDetector.cpp
    Source/CircularBuffer.h)

# Compile definitions
target_compile_definitions(YINPitchDetectorApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:YINPitchDetectorApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:YINPitchDetectorApp,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Link JUCE libraries
target_link_libraries(YINPitchDetectorApp PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_dsp
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_gui_extra)