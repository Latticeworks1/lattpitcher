#!/usr/bin/env python3
"""
Simple Real-Time Audio Code Linter for JUCE Applications
"""

import re
import os
import sys
import glob
from pathlib import Path

class RTAudioLinter:
    def __init__(self):
        self.critical_patterns = [
            (r'\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b', 
             'Dynamic memory allocation in audio thread causes dropouts'),
            (r'\.push_back\(|\.insert\(|\.resize\(', 
             'std::vector operations can allocate memory in audio thread'),
            (r'std::string\s+\w+\s*=|String\s+\w+\s*=', 
             'String operations allocate memory in audio thread'),
            (r'\bthrow\s|\btry\s*\{|\bcatch\s*\(', 
             'Exceptions break real-time determinism'),
            (r'processBlock.*repaint|processBlock.*MessageManager', 
             'GUI operations in processBlock break real-time safety'),
        ]
        
        # Whitelist patterns for known safe contexts
        self.safe_context_patterns = [
            r'createEditor\(\)',
            r'createPluginFilter\(\)', 
            r'timerCallback\(\)',
            r'paint\(Graphics',
            r'resized\(\)',
            r'clearHistory\(\)',
            r'addNote\(',
            r'// Factory method',
            r'// GUI operation',
            r'// JUCE framework'
        ]
        
        self.high_patterns = [
            (r'\.load\(\)|\.store\(', 
             'Atomic operations should use explicit memory ordering'),
            (r'CriticalSection.*\.enter\(|std::mutex.*\.lock\(', 
             'Blocking locks in audio thread cause priority inversion'),
            (r'\bint\s+\w*Index\s*=|\bint\s+\w*Count\s*=', 
             'Non-atomic shared variable creates race condition'),
            (r'buffer\[|audio\[', 
             'Potential buffer overflow without bounds checking'),
        ]
        
        self.medium_patterns = [
            (r'\bsin\(|\bcos\(|\btan\(|\bexp\(|\blog\(|\bpow\(|\bsqrt\(', 
             'Transcendental math functions are expensive in audio thread'),
            (r'for.*for.*buffer|for.*for.*audio', 
             'Nested loops in audio processing suggest O(n²) complexity'),
        ]
    
    def lint_file(self, filepath):
        if not os.path.exists(filepath):
            return []
        
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        
        issues = []
        lines = content.split('\n')
        
        # Check if this is an audio processing file
        is_audio_file = any(keyword in content.lower() 
                           for keyword in ['processblock', 'audio', 'sample', 'buffer'])
        
        if not is_audio_file:
            return issues
        
        for line_num, line in enumerate(lines, 1):
            # Skip comments
            if re.match(r'^\s*(?://|/\*|\*|$)', line):
                continue
            
            # Check critical patterns
            for pattern, message in self.critical_patterns:
                if re.search(pattern, line, re.IGNORECASE):
                    # Check if this line is in a safe context
                    is_safe_context = False
                    for safe_pattern in self.safe_context_patterns:
                        if re.search(safe_pattern, line, re.IGNORECASE):
                            is_safe_context = True
                            break
                    
                    # Also check surrounding lines for context
                    if not is_safe_context:
                        try:
                            with open(filepath, 'r') as f:
                                all_lines = f.read().split('\n')
                            context_start = max(0, line_num - 10)
                            context_end = min(len(all_lines), line_num + 5)
                            context = '\n'.join(all_lines[context_start:context_end])
                            
                            for safe_pattern in self.safe_context_patterns:
                                if re.search(safe_pattern, context, re.IGNORECASE):
                                    is_safe_context = True
                                    break
                        except:
                            pass
                    
                    if not is_safe_context:
                        issues.append({
                            'file': filepath,
                            'line': line_num,
                            'severity': 'CRITICAL',
                            'message': message,
                            'code': line.strip()
                        })
            
            # Check high severity patterns
            for pattern, message in self.high_patterns:
                if re.search(pattern, line, re.IGNORECASE):
                    issues.append({
                        'file': filepath,
                        'line': line_num,
                        'severity': 'HIGH',
                        'message': message,
                        'code': line.strip()
                    })
            
            # Check medium severity patterns
            for pattern, message in self.medium_patterns:
                if re.search(pattern, line, re.IGNORECASE):
                    issues.append({
                        'file': filepath,
                        'line': line_num,
                        'severity': 'MEDIUM',
                        'message': message,
                        'code': line.strip()
                    })
        
        return issues
    
    def format_report(self, issues):
        if not issues:
            return "✅ No real-time audio issues found!\n"
        
        report = ["🚨 REAL-TIME AUDIO CODE ANALYSIS REPORT 🚨\n"]
        
        # Group by severity
        critical = [i for i in issues if i['severity'] == 'CRITICAL']
        high = [i for i in issues if i['severity'] == 'HIGH']
        medium = [i for i in issues if i['severity'] == 'MEDIUM']
        
        if critical:
            report.append(f"❌ CRITICAL: {len(critical)} issues will cause audio dropouts/crashes")
        if high:
            report.append(f"⚠️  HIGH: {len(high)} issues will cause performance problems")
        if medium:
            report.append(f"ℹ️  MEDIUM: {len(medium)} issues affect best practices")
        
        report.append(f"📊 Total issues: {len(issues)}\n")
        
        # Show issues by severity
        for severity, severity_issues in [('CRITICAL', critical), ('HIGH', high), ('MEDIUM', medium)]:
            if not severity_issues:
                continue
            
            report.append(f"\n{'='*50}")
            report.append(f"{severity} ISSUES ({len(severity_issues)})")
            report.append(f"{'='*50}")
            
            for issue in severity_issues:
                report.append(f"\n📁 {issue['file']}:{issue['line']}")
                report.append(f"📝 {issue['message']}")
                report.append(f"💻 {issue['code']}")
        
        return '\n'.join(report)

def main():
    if len(sys.argv) < 2:
        print("Usage: python rt_audio_linter_simple.py <file1> [file2] ...")
        sys.exit(1)
    
    linter = RTAudioLinter()
    all_issues = []
    
    for filepath in sys.argv[1:]:
        if os.path.exists(filepath):
            issues = linter.lint_file(filepath)
            all_issues.extend(issues)
    
    print(linter.format_report(all_issues))
    
    # Exit code based on severity
    if any(i['severity'] == 'CRITICAL' for i in all_issues):
        sys.exit(2)
    elif any(i['severity'] == 'HIGH' for i in all_issues):
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()