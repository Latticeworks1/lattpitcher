cmake_minimum_required(VERSION 3.22)
project(NetworkAudioStreamer VERSION 1.0.0)

# C++17 required for uWebSockets and modern threading
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point to JUCE directory
add_subdirectory(../JUCE ../JUCE_build)

# Find required packages
find_package(PkgConfig REQUIRED)

# Find Opus codec
pkg_check_modules(OPUS REQUIRED opus)

# Add explicit library paths for Opus
link_directories(/opt/local/lib)

# Find uWebSockets (will be added as submodule or manual installation)
set(UWS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/src")

# Common source files
set(COMMON_SOURCES
    Source/NetworkAudioProcessor.cpp
    Source/NetworkAudioEditor.cpp
    Source/NetworkManager.cpp
    Source/AudioMonitor.cpp
    Source/SpectrumAnalyzer.cpp
    Source/DebugConsole.cpp
    Source/NetworkControlPanel.cpp
    Source/ModernTheme.cpp
)

# Common JUCE libraries
set(COMMON_JUCE_LIBS
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
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
# Audio Plugin (VST3/AU for FL Studio)
# =============================================================================
juce_add_plugin(NetworkAudioStreamerPlugin
    PRODUCT_NAME "NetworkAudioStreamer"
    VERSION "1.0.0"
    COMPANY_NAME "Audio Streaming Solutions"
    BUNDLE_ID "com.audiostreaming.networkaudiostreamer"
    PLUGIN_MANUFACTURER_CODE "AuSt"
    PLUGIN_CODE "NtAs"
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "/Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/VST3"
    AU_COPY_DIR "/Users/m1a4xnetworkprobe./Library/Audio/Plug-Ins/Components"
    PLUGIN_DESCRIPTION "Real-time audio streaming between FL Studio instances using WebSockets and Opus codec"
    PLUGIN_CATEGORY "Effect"
)

target_sources(NetworkAudioStreamerPlugin PRIVATE
    ${COMMON_SOURCES}
)

target_link_libraries(NetworkAudioStreamerPlugin PRIVATE
    ${COMMON_JUCE_LIBS}
    ${OPUS_LIBRARIES}
    opus  # Direct opus library link
)

# Include directories
target_include_directories(NetworkAudioStreamerPlugin PRIVATE
    Source
    ${UWS_INCLUDE_DIR}
    ${OPUS_INCLUDE_DIRS}
)

# Compiler flags for Opus
target_compile_options(NetworkAudioStreamerPlugin PRIVATE
    ${OPUS_CFLAGS_OTHER}
)

# Preprocessor definitions for plugin
target_compile_definitions(NetworkAudioStreamerPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    # Network audio streaming specific
    NETWORK_AUDIO_BUFFER_SIZE=4096
    OPUS_FRAME_SIZE_MS=5  # Ultra-low latency 5ms frames
    DEFAULT_NETWORK_PORT=9001
    DEFAULT_HTTP_PORT=8080
    MAX_NETWORK_CLIENTS=8
    MAX_WEB_PEERS=16
    # Web networking features
    ENABLE_WEB_NETWORKING=1
    ENABLE_HTTP_SERVER=1
    ENABLE_WEBRTC=1
    ENABLE_DISCOVERY=1
)

# Platform-specific settings
if(APPLE)
    target_compile_definitions(NetworkAudioStreamerPlugin PRIVATE
        JUCE_MAC=1
    )
    # Link against system networking frameworks
    target_link_libraries(NetworkAudioStreamerPlugin PRIVATE
        "-framework Foundation"
        "-framework SystemConfiguration"
    )
elseif(WIN32)
    target_compile_definitions(NetworkAudioStreamerPlugin PRIVATE
        JUCE_WINDOWS=1
        _WIN32_WINNT=0x0601  # Windows 7 minimum
    )
    # Link against Winsock for networking
    target_link_libraries(NetworkAudioStreamerPlugin PRIVATE
        ws2_32
        wsock32
    )
endif()

# =============================================================================
# Standalone Application for Testing
# =============================================================================
juce_add_gui_app(NetworkAudioStreamerApp
    PRODUCT_NAME "Network Audio Streamer"
    VERSION "1.0.0"
    COMPANY_NAME "Audio Streaming Solutions"
    BUNDLE_ID "com.audiostreaming.networkaudiostreamer.app"
    ICON_BIG ""
    ICON_SMALL ""
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Network Audio Streamer requires microphone access to stream audio input in real-time to other connected FL Studio instances. Audio data is transmitted over local network connections only."
)

target_sources(NetworkAudioStreamerApp PRIVATE
    Main.cpp
    ${COMMON_SOURCES}
)

target_link_libraries(NetworkAudioStreamerApp PRIVATE
    ${COMMON_JUCE_LIBS}
)

target_include_directories(NetworkAudioStreamerApp PRIVATE
    Source
    ${UWS_INCLUDE_DIR}
    ${OPUS_INCLUDE_DIRS}
)

target_compile_options(NetworkAudioStreamerApp PRIVATE
    ${OPUS_CFLAGS_OTHER}
)

# =============================================================================
# Unit Tests (Disabled - test files not created yet)
# =============================================================================
# enable_testing()

# juce_add_console_app(NetworkAudioStreamerTests
#     PRODUCT_NAME "Network Audio Streamer Tests"
# )

# target_sources(NetworkAudioStreamerTests PRIVATE
#     Tests/NetworkTests.cpp
#     Tests/OpusCodecTests.cpp
#     Tests/LatencyTests.cpp
#     Tests/BufferTests.cpp
#     ${COMMON_SOURCES}
# )

# target_link_libraries(NetworkAudioStreamerTests PRIVATE
#     ${COMMON_JUCE_LIBS}
#     ${OPUS_LIBRARIES}
# )

# target_include_directories(NetworkAudioStreamerTests PRIVATE
#     Source
#     ${UWS_INCLUDE_DIR}
#     ${OPUS_INCLUDE_DIRS}
# )

# # Add test discovery
# add_test(NAME NetworkAudioStreamerUnitTests COMMAND NetworkAudioStreamerTests)

# =============================================================================
# Third-party Dependencies Setup Instructions
# =============================================================================
# NOTE: Before building, ensure the following dependencies are installed:
#
# 1. Opus Codec:
#    macOS: brew install opus
#    Ubuntu: sudo apt-get install libopus-dev
#    Windows: vcpkg install opus
#
# 2. uWebSockets:
#    git submodule add https://github.com/uNetworking/uWebSockets.git third_party/uWebSockets
#    cd third_party/uWebSockets
#    git submodule update --init --recursive
#
# Build Instructions:
#    mkdir build && cd build
#    cmake ..
#    make -j4
#    
#    The VST3 plugin will be automatically copied to your system plugins directory
#    Load in FL Studio and configure as Server on one instance, Client on another
