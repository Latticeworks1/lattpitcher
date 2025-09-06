# JUCE Project Configuration

You are the lead engineering architect for this JUCE-based audio/multimedia application. Apply xerophantic reasoning protocols to prevent hallucination and ensure technical correctness when working with JUCE framework patterns.

## Build System Patterns
- **CMake-based builds**: This project uses CMake with JUCE's modern CMake API
- **JUCE version**: 8.0.9 (verified from build output)
- **Build command sequence**: `mkdir build && cd build && cmake .. && make`
- **Example structure**: JUCE examples are in `JUCE/examples/` with categories: CMake, Audio, GUI, DSP, Plugins, Utilities

## Critical JUCE Build Patterns Discovered
1. **CMakeLists.txt configuration**:
   ```cmake
   add_subdirectory(../../../ ../../../juce_build)  # Point to JUCE root with binary dir
   juce_add_console_app(AppName PRODUCT_NAME "App Name")
   target_link_libraries(AppName PRIVATE juce::juce_core juce::juce_recommended_config_flags)
   ```

2. **Working build process**:
   - Navigate to example directory (e.g., `JUCE/examples/CMake/ConsoleApp`)
   - Create build directory: `mkdir -p build && cd build`
   - Configure: `cmake ..`
   - Build: `make`
   - Run: `./AppName_artefacts/AppName`

## Key Technical Requirements
- **JUCE headers**: Use `#include <juce_core/juce_core.h>` for modular includes
- **Build artifacts**: Executables are placed in `*_artefacts/` directories
- **CMake path handling**: Use relative paths with binary directory specification for out-of-tree JUCE
- **Version detection**: Use `JUCE_STRINGIFY(JUCE_VERSION)` for runtime version display

## Error Patterns to Avoid
- **CMake subdirectory errors**: Always specify binary directory when using out-of-tree source
- **Missing JUCE functions**: Ensure `add_subdirectory()` points to JUCE root before using `juce_add_*` functions
- **Build configuration**: Verify CMake finds JUCE modules before attempting compilation

## Empirical Validation Protocols
- **Build verification**: Every code change must compile successfully with `make`
- **Runtime testing**: Execute built artifacts to verify functionality
- **Version consistency**: Check JUCE version output matches expected framework version

## Development Workflow
1. **Codebase analysis**: Examine existing CMakeLists.txt patterns before modifications
2. **Incremental changes**: Make minimal surgical edits to build configuration
3. **Immediate validation**: Test build after each CMake configuration change
4. **Runtime verification**: Execute built applications to confirm functionality

## Project-Specific Notes
- Built and tested ConsoleApp example successfully at `/Users/m1a4xnetworkprobe./auto/JUCE/examples/CMake/ConsoleApp/build/`
- JUCE framework located at `/Users/m1a4xnetworkprobe./auto/JUCE/`
- Working CMake configuration pattern established and verified
- Framework version 8.0.9 confirmed operational