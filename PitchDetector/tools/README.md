# Real-Time Audio Code Linter

This tool helps ensure your JUCE audio code follows real-time safety best practices.

## Installation

```bash
# Install git hooks (run once in your repo)
python tools/rt_audio_linter.py --install-hooks

# Or use the simpler version
python tools/rt_audio_linter_simple.py <files>
```

## Usage

```bash
# Lint specific files
python tools/rt_audio_linter_simple.py Source/PitchDetector.h Source/PitchDetector.cpp

# Lint all C++ files
python tools/rt_audio_linter_simple.py Source/*.h Source/*.cpp

# The tool automatically detects audio processing files and applies stricter rules
```

## What It Catches

### CRITICAL Issues (Will cause audio dropouts/crashes)
- Dynamic memory allocation (`malloc`, `new`, `vector.push_back()`)
- String operations that allocate memory
- Exception handling (`try`/`catch`/`throw`)
- GUI operations in `processBlock()`
- File I/O operations

### HIGH Issues (Will cause performance problems)
- Non-atomic shared variables
- Blocking locks in audio threads
- Buffer access without bounds checking
- Atomic operations without explicit memory ordering

### MEDIUM Issues (Best practices)
- Expensive math functions (`sin`, `cos`, `sqrt`)
- O(n²) nested loops in audio processing
- Division operations (prefer multiplication by reciprocal)

## Git Hook Integration

When installed, the pre-commit hook will:
- Run automatically before each commit
- Block commits with CRITICAL issues
- Warn about HIGH severity issues but allow commit
- Show exactly which lines need fixing

## Exit Codes

- `0`: No issues or only low severity
- `1`: HIGH severity issues found
- `2`: CRITICAL issues found (blocks commit)

## Configuration

Edit `.rt_audio_linter.json` to customize:
- Disable specific rules
- Set severity thresholds
- Exclude certain files/patterns

## Example Output

```
🚨 REAL-TIME AUDIO CODE ANALYSIS REPORT 🚨

❌ CRITICAL: 2 issues will cause audio dropouts/crashes
⚠️  HIGH: 5 issues will cause performance problems

==================================================
CRITICAL ISSUES (2)
==================================================

📁 Source/AutotuneEngine.cpp:42
📝 std::vector operations can allocate memory in audio thread
💻 pitchBuffer.push_back(frequency);

📁 Source/AutotuneEngine.cpp:67
📝 Dynamic memory allocation in audio thread causes dropouts
💻 float* tempBuffer = new float[bufferSize];
```

## Integration with Build Systems

### CMake Integration
```cmake
add_custom_target(lint
    COMMAND python tools/rt_audio_linter_simple.py Source/*.cpp Source/*.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running real-time audio linter"
)
```

### CI/CD Integration
```yaml
- name: Lint Audio Code
  run: python tools/rt_audio_linter_simple.py Source/
  # Fails CI if critical issues found
```

## Why This Matters

Real-time audio processing has strict requirements:
- **No memory allocation** - causes audio dropouts
- **No blocking operations** - breaks real-time guarantees  
- **Deterministic execution** - consistent latency required
- **Thread safety** - multiple threads access audio data

This linter helps catch violations before they cause problems in production DAWs and live performance situations.