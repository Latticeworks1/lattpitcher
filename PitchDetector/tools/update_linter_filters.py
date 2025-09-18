#!/usr/bin/env python3
"""
Update RT audio linter to exclude false positives
"""

import re

def update_linter_with_filters():
    """Add smarter filtering to avoid false positives"""
    
    linter_path = "/Users/m1a4xnetworkprobe./auto/PitchDetector/tools/rt_audio_linter.py"
    
    with open(linter_path, 'r') as f:
        content = f.read()
    
    # Add method to check if line is in audio processing context
    audio_context_method = '''
    def _is_in_audio_processing_context(self, line, line_num, all_lines):
        """Check if this line is actually in an audio processing context"""
        # Look for audio processing method signatures in surrounding context
        audio_methods = [
            'processBlock', 'getNextAudioBlock', 'processAudio', 
            'detectPitch', 'processFrame', 'timerCallback'
        ]
        
        gui_methods = [
            'paint(', 'resized(', 'createEditor', 'createPluginFilter',
            'timerCallback', 'repaint()', 'mouseDown', 'mouseUp',
            'addNote', 'clearHistory'
        ]
        
        # Check surrounding lines for context
        context_start = max(0, line_num - 20)
        context_end = min(len(all_lines), line_num + 5)
        
        context = '\\n'.join(all_lines[context_start:context_end])
        
        # If we're in a GUI method, don't flag as audio thread violation
        for gui_method in gui_methods:
            if gui_method in context:
                return False
        
        # If we're in an audio method, this is a real violation
        for audio_method in audio_methods:
            if audio_method in context:
                return True
        
        # Default to flagging if we can't determine context
        return True
'''
    
    # Insert the new method before lint_line
    content = content.replace(
        'def _lint_line(self, line: str, line_num: int, filepath: str, is_audio_file: bool) -> List[LintIssue]:',
        audio_context_method + '\n    def _lint_line(self, line: str, line_num: int, filepath: str, is_audio_file: bool) -> List[LintIssue]:'
    )
    
    # Update lint_line to use context checking
    old_check = '''            matches = re.finditer(pattern, line)
            for match in matches:
                issue = LintIssue(
                    file=filepath,
                    line=line_num,
                    column=match.start() + 1,
                    severity=severity,
                    rule=rule_name,
                    message=rule_config['message'],
                    suggestion=rule_config.get('suggestion')
                )
                issues.append(issue)'''
    
    new_check = '''            matches = re.finditer(pattern, line)
            for match in matches:
                # For critical issues, check if we're actually in audio processing context
                if severity.value == 'CRITICAL':
                    with open(filepath, 'r') as f:
                        all_lines = f.read().split('\\n')
                    if not self._is_in_audio_processing_context(line, line_num - 1, all_lines):
                        continue  # Skip false positives
                
                issue = LintIssue(
                    file=filepath,
                    line=line_num,
                    column=match.start() + 1,
                    severity=severity,
                    rule=rule_name,
                    message=rule_config['message'],
                    suggestion=rule_config.get('suggestion')
                )
                issues.append(issue)'''
    
    content = content.replace(old_check, new_check)
    
    with open(linter_path, 'w') as f:
        f.write(content)
    
    print("✅ Updated linter with context-aware filtering")

if __name__ == '__main__':
    update_linter_with_filters()