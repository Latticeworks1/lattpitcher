cmake_minimum_required(VERSION 3.22)
project(FLStreamPlugin VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress UTF-8 compatibility warnings
if(APPLE OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-pre-c++17-compat)
endif()

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# Find Opus codec for audio compression
pkg_check_modules(OPUS opus)

# Add explicit library paths for Opus and Boost
if(APPLE)
    link_directories(/opt/local/lib /usr/local/lib)
endif()

# Third party libraries
set(WEBSOCKETPP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/websocketpp")
set(LIBWEBM_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libwebm")

# Build libwebm
add_subdirectory(${LIBWEBM_PATH} libwebm)

# WebClient path for serving files
set(WEBCLIENT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/WebClient")

# Add JUCE as a subdirectory
if(NOT TARGET juce::juce_audio_processors)
    add_subdirectory(../JUCE JUCE)
endif()

# Primary FL Stream Plugin (Generator/Instrument - for mixer routing and recording)
juce_add_plugin(FLStreamPlugin
    COMPANY_NAME "YourCompany"
    PLUGIN_MANUFACTURER_CODE "Manu"
    PLUGIN_CODE "FLSt"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "FL Stream"
    VST3_CATEGORIES "Instrument" "Synth" "Network"
    AU_MAIN_TYPE "kAudioUnitType_MusicDevice"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# Legacy Effect Plugin (optional - for audio track insertion)
juce_add_plugin(FLStreamEffect
    COMPANY_NAME "YourCompany"
    PLUGIN_MANUFACTURER_CODE "Manu"
    PLUGIN_CODE "FLSe"
    FORMATS VST3 AU
    PRODUCT_NAME "FL Stream Effect"
    VST3_CATEGORIES "Effect" "Network"
    AU_MAIN_TYPE "kAudioUnitType_MusicEffect"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# Sources - Primary FL Stream Plugin (Generator/Instrument)
target_sources(FLStreamPlugin
    PRIVATE
        # WebView-based implementation with Colyseus room management
        Source/FLStreamProcessor.cpp
        Source/FLStreamProcessor.h
        Source/FLStreamEditor.cpp
        Source/FLStreamEditor.h
        Source/ColyseusRoomClient.cpp
        Source/ColyseusRoomClient.h
)

# Sources - Legacy FL Stream Effect Plugin (optional)
target_sources(FLStreamEffect
    PRIVATE
        # WebView-based implementation with Colyseus room management
        Source/FLStreamProcessor.cpp
        Source/FLStreamProcessor.h
        Source/FLStreamEditor.cpp
        Source/FLStreamEditor.h
        Source/ColyseusRoomClient.cpp
        Source/ColyseusRoomClient.h
)

# Include directories for Primary FL Stream Plugin (Generator)
target_include_directories(FLStreamPlugin PRIVATE 
    ${CURL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${WEBSOCKETPP_PATH}
    ${LIBWEBM_PATH}
    Source
    ${OPUS_INCLUDE_DIRS}
)

# Include directories for Legacy Effect Plugin
target_include_directories(FLStreamEffect PRIVATE 
    ${CURL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${WEBSOCKETPP_PATH}
    ${LIBWEBM_PATH}
    Source
    ${OPUS_INCLUDE_DIRS}
)

# Libraries for Primary FL Stream Plugin (Generator)
target_link_libraries(FLStreamPlugin
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        juce::juce_core
        juce::juce_events
        juce::juce_gui_basics
        juce::juce_gui_extra
        ${Boost_LIBRARIES}
        ${CURL_LIBRARIES}
        ${OPUS_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        webm
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Libraries for Legacy Effect Plugin
target_link_libraries(FLStreamEffect
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        juce::juce_core
        juce::juce_events
        juce::juce_gui_basics
        juce::juce_gui_extra
        ${Boost_LIBRARIES}
        ${CURL_LIBRARIES}
        ${OPUS_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        webm
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Compiler flags for Opus
if(OPUS_FOUND)
    target_compile_options(FLStreamPlugin PRIVATE ${OPUS_CFLAGS_OTHER})
    target_compile_options(FLStreamEffect PRIVATE ${OPUS_CFLAGS_OTHER})
endif()

# Preprocessor definitions for Primary FL Stream Plugin (Generator)
target_compile_definitions(FLStreamPlugin
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        WEBCLIENT_PATH="${WEBCLIENT_PATH}"
)

# Preprocessor definitions for Legacy Effect Plugin
target_compile_definitions(FLStreamEffect
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        WEBCLIENT_PATH="${WEBCLIENT_PATH}"
)

# macOS specific settings
if(APPLE)
    target_link_libraries(FLStreamPlugin PRIVATE
        "-framework Foundation"
        "-framework SystemConfiguration"
    )
    target_link_libraries(FLStreamEffect PRIVATE
        "-framework Foundation"
        "-framework SystemConfiguration"
    )
endif()

# Note: Test executables removed - tests/ directory has been cleaned up
