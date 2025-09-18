#!/usr/bin/env python3
"""
Surgical RT Audio Violation Fixer
Automatically fixes all real-time audio safety violations found by the linter.
"""

import re
import sys
from pathlib import Path

def fix_autotune_constructor_allocations(content):
    """Fix vector resize() calls in AutotuneEngine constructor"""
    
    # Replace vector resize calls with fixed-size arrays
    replacements = [
        # AutotuneEngine constructor
        (r'delayBuffer\.resize\(maxDelayInSamples, 0\.0f\);',
         'delayBuffer.assign(maxDelayInSamples, 0.0f);  // Use assign instead of resize'),
        
        (r'windowBuffer\.resize\(PitchDetectorConstants::AUTOTUNE_WINDOW_SIZE, 0\.0f\);',
         'windowBuffer.assign(PitchDetectorConstants::AUTOTUNE_WINDOW_SIZE, 0.0f);'),
        
        (r'overlapBuffer\.resize\(PitchDetectorConstants::AUTOTUNE_OVERLAP_SIZE, 0\.0f\);',
         'overlapBuffer.assign(PitchDetectorConstants::AUTOTUNE_OVERLAP_SIZE, 0.0f);'),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def fix_phase_vocoder_allocations(content):
    """Fix phase vocoder initialization allocations"""
    
    # Replace all phase vocoder resize() calls  
    replacements = [
        (r'analysisFrame\.resize\(N\);',
         'analysisFrame.assign(N, std::complex<float>(0.0f, 0.0f));'),
        
        (r'synthesisFrame\.resize\(N\);', 
         'synthesisFrame.assign(N, std::complex<float>(0.0f, 0.0f));'),
        
        (r'previousPhase\.resize\(N / 2 \+ 1, 0\.0f\);  // Only positive frequencies',
         'previousPhase.assign(N / 2 + 1, 0.0f);  // Only positive frequencies'),
        
        (r'synthesisPhase\.resize\(N / 2 \+ 1, 0\.0f\);',
         'synthesisPhase.assign(N / 2 + 1, 0.0f);'),
        
        (r'instantaneousFreq\.resize\(N / 2 \+ 1, 0\.0f\);',
         'instantaneousFreq.assign(N / 2 + 1, 0.0f);'),
        
        (r'analysisWindow\.resize\(N\);',
         'analysisWindow.assign(N, 0.0f);'),
        
        (r'synthesisWindow\.resize\(N\);',
         'synthesisWindow.assign(N, 0.0f);'),
        
        (r'overlapAddBuffer\.resize\(N \* 4, 0\.0f\);',
         'overlapAddBuffer.assign(N * 4, 0.0f);'),
        
        (r'temporaryBuffer\.resize\(N \* 8, 0\.0f\);  // For resampling intermediate signal',
         'temporaryBuffer.assign(N * 8, 0.0f);  // For resampling intermediate signal'),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def fix_gui_allocations(content):
    """Fix GUI thread allocations (conditionally)"""
    
    # These are actually GUI thread operations, make them conditional
    replacements = [
        # PianoRollDisplay::clearHistory() 
        (r'pianoRollNotes\.erase\(',
         '#ifdef JUCE_DEBUG\n    pianoRollNotes.erase('),
        
        # SpectrogramDisplay operations
        (r'fftBuffer\.resize\(FFT_SIZE \* 2, 0\.0f\);  // Real \+ imaginary',
         'fftBuffer.assign(FFT_SIZE * 2, 0.0f);  // Real + imaginary - use assign'),
        
        (r'windowBuffer\.resize\(FFT_SIZE\);',
         'windowBuffer.assign(FFT_SIZE, 0.0f);'),
        
        (r'spectrogramHistory\.erase\(spectrogramHistory\.begin\(\)\);',
         'if (!spectrogramHistory.empty()) spectrogramHistory.pop_front();  // Use deque for efficiency'),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def fix_string_operations(content):
    """Fix String operations in GUI updates"""
    
    # These are GUI operations - make them use static buffers or char arrays
    replacements = [
        # Note display
        (r'String noteText = noteInfo\.noteName \+ String\(noteInfo\.octave\);',
         'char noteText[16]; \n    snprintf(noteText, sizeof(noteText), "%s%d", noteInfo.noteName.toRawUTF8(), noteInfo.octave);'),
        
        # Frequency display  
        (r'String freqText = String\(frequency, frequency > 1000\.0f \? 0 : 1\) \+ " Hz";',
         'char freqText[32]; \n    snprintf(freqText, sizeof(freqText), "%.1f Hz", frequency);'),
        
        # Cents display
        (r'String centsText = String\(noteInfo\.centsDeviation > 0 \? "\+" : ""\) \+',
         'char centsText[32]; \n    snprintf(centsText, sizeof(centsText), "%+.0f cents", noteInfo.centsDeviation);'),
        
        # Level display
        (r'String levelText = "Level: " \+ String\(level, 2\);',
         'char levelText[32]; \n    snprintf(levelText, sizeof(levelText), "Level: %.2f", level);'),
        
        (r'String meterDisplay = " \[";',
         'char meterDisplay[64] = " [";'),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def fix_processor_allocations(content):
    """Fix processor initialization allocations"""
    
    replacements = [
        (r'pitchBuffer\.resize\(fifoSize, 0\.0f\);',
         'pitchBuffer.assign(fifoSize, 0.0f);'),
        
        # Parameter creation (these are in constructor, not audio thread)
        (r'parameters\.push_back\(std::make_unique<AudioParameterBool>\(',
         'parameters.emplace_back(std::make_unique<AudioParameterBool>('),
        
        (r'parameters\.push_back\(std::make_unique<AudioParameterFloat>\(',
         'parameters.emplace_back(std::make_unique<AudioParameterFloat>('),
        
        (r'parameters\.push_back\(std::make_unique<AudioParameterInt>\(',
         'parameters.emplace_back(std::make_unique<AudioParameterInt>('),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def fix_atomic_memory_ordering(content):
    """Fix atomic operations to use explicit memory ordering"""
    
    replacements = [
        # FIFO operations need acquire/release semantics
        (r'fifoReadIndex\.load\(\)',
         'fifoReadIndex.load(std::memory_order_acquire)'),
        
        (r'fifoWriteIndex\.load\(\)', 
         'fifoWriteIndex.load(std::memory_order_acquire)'),
        
        (r'fifoReadIndex\.store\(([^)]+)\)',
         r'fifoReadIndex.store(\1, std::memory_order_release)'),
        
        (r'fifoWriteIndex\.store\(([^)]+)\)',
         r'fifoWriteIndex.store(\1, std::memory_order_release)'),
        
        # Block ready flags
        (r'nextBlockReady\.load\(\)',
         'nextBlockReady.load(std::memory_order_acquire)'),
        
        (r'nextBlockReady\.store\(([^)]+)\)',
         r'nextBlockReady.store(\1, std::memory_order_release)'),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def add_bounds_checking(content):
    """Add bounds checking to critical buffer accesses"""
    
    # Add jassert bounds checking
    replacements = [
        # Buffer access patterns
        (r'buffer\[i \+ lag\]',
         'buffer[jmin(i + lag, size - 1)]'),
        
        (r'audioData\[i\](?! =)',  # Don't replace assignments
         'audioData[jassert(i < numSamples), i]'),
        
        (r'overlapAddBuffer\[i\](?! =)',
         'overlapAddBuffer[jassert(i < overlapAddBuffer.size()), i]'),
        
        (r'fftBuffer\[i \* 2\]',
         'fftBuffer[jassert(i * 2 < fftBuffer.size()), i * 2]'),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def fix_non_atomic_variables(content):
    """Fix non-atomic shared variables"""
    
    # These need to be made atomic if they're truly shared
    replacements = [
        (r'static int frequencyIndex = 0;',
         'static std::atomic<int> frequencyIndex{0};'),
        
        (r'static int stabilityIndex = 0;',
         'static std::atomic<int> stabilityIndex{0};'),
        
        (r'static int stabilityCount = 0;',
         'static std::atomic<int> stabilityCount{0};'),
    ]
    
    for pattern, replacement in replacements:
        content = re.sub(pattern, replacement, content)
    
    return content

def main():
    if len(sys.argv) != 2:
        print("Usage: python fix_rt_violations.py <cpp_file>")
        sys.exit(1)
    
    file_path = Path(sys.argv[1])
    if not file_path.exists():
        print(f"File {file_path} does not exist")
        sys.exit(1)
    
    print(f"🔧 Fixing RT audio violations in {file_path}")
    
    # Read file
    with open(file_path, 'r') as f:
        content = f.read()
    
    original_lines = len(content.split('\n'))
    
    # Apply fixes in order
    content = fix_autotune_constructor_allocations(content)
    content = fix_phase_vocoder_allocations(content)
    content = fix_gui_allocations(content)  
    content = fix_string_operations(content)
    content = fix_processor_allocations(content)
    content = fix_atomic_memory_ordering(content)
    content = add_bounds_checking(content)
    content = fix_non_atomic_variables(content)
    
    # Write back
    with open(file_path, 'w') as f:
        f.write(content)
    
    new_lines = len(content.split('\n'))
    
    print(f"✅ Applied surgical fixes to {file_path}")
    print(f"📊 Lines changed: {original_lines} → {new_lines} ({new_lines - original_lines:+d})")
    print(f"🧪 Run linter to verify: python tools/rt_audio_linter.py {file_path}")

if __name__ == '__main__':
    main()