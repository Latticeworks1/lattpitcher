# Gemini Codebase Guide

This document provides instructions on how to build and run the various components of this project.

## Project Overview

This project consists of two main parts:

1.  **A C++ JUCE application** named `PitchDetector`. This is the core of the project, providing real-time audio pitch detection as both a standalone GUI application and as audio plugins (VST3 and AU).
2.  **A Rust application** named `pitch_detector`. This is a secondary component with a minimal setup.

## C++ JUCE Application (`PitchDetector`)

The C++ application is built using CMake and the JUCE framework.

### Dependencies

*   CMake 3.22 or higher
*   A C++17 compatible compiler (e.g., Clang, GCC, MSVC)
*   The JUCE submodule, which is included in this repository.

### Building

To build the JUCE application, follow these steps:

1.  **Configure with CMake:**
    ```bash
    cmake -B PitchDetector/build -S PitchDetector
    ```

2.  **Build with CMake:**
    ```bash
    cmake --build PitchDetector/build
    ```

### Running

The build process generates a standalone application and audio plugins.

*   **Standalone Application:** The executable, `PitchDetectorApp`, can be found in the `PitchDetector/build/bin` directory.
*   **Audio Plugins:** The VST3 and AU plugins are automatically copied to the standard system plugin locations:
    *   **VST3:** `~/Library/Audio/Plug-Ins/VST3/`
    *   **AU:** `~/Library/Audio/Plug-Ins/Components/`

You can run the standalone application directly or load the plugins in a compatible Digital Audio Workstation (DAW).

## Rust Application (`pitch_detector`)

The Rust application is a simple command-line program managed with Cargo.

### Building

To build the Rust application, run:

```bash
cargo build
```

### Running

To run the application, use:

```bash
cargo run
```

### Testing

To run tests for the Rust application, use:

```bash
cargo test
```
