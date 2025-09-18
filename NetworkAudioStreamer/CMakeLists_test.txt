cmake_minimum_required(VERSION 3.22)
project(MinimalNetworkTest VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Point to JUCE
add_subdirectory(../JUCE ../JUCE_test_build)

# Simple test app
juce_add_gui_app(MinimalNetworkTest
    PRODUCT_NAME "Minimal Network Test"
    VERSION "1.0.0"
    COMPANY_NAME "Test"
    BUNDLE_ID "com.test.minimal"
)

target_sources(MinimalNetworkTest PRIVATE
    minimal_test.cpp
)

target_link_libraries(MinimalNetworkTest PRIVATE
    juce::juce_core
    juce::juce_events  
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
)

target_compile_definitions(MinimalNetworkTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)