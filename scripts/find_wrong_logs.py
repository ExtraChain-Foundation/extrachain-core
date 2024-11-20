import re
import glob
from dataclasses import dataclass
from typing import List
from pathlib import Path
import os

@dataclass
class Issue:
    file: str
    line_number: int
    line: str
    issue_type: str

def join_multiline_logs(lines: List[str], start_idx: int) -> tuple[str, int]:
    result = []
    parentheses_count = 0
    idx = start_idx
    
    while idx < len(lines):
        line = lines[idx].strip()
        result.append(line)
        
        parentheses_count += line.count('(') - line.count(')')
        if parentheses_count == 0 and line.endswith(';'):
            break
            
        idx += 1
        
    return ' '.join(result), idx - start_idx + 1

def find_multiple_spaces(line: str) -> bool:
    pattern = r'e(?:Log|Info|Warning|Critical|Fatal)\s*\(\s*"[^"]*?\s{2,}[^"]*?"'
    return bool(re.search(pattern, line))

def find_arg_or_format(line: str) -> bool:
    return '.arg(' in line or 'fmt::format' in line

def find_trailing_dot(line: str) -> bool:
    pattern = r'e(?:Log|Info|Warning|Critical|Fatal)\s*\(\s*"[^"]*?\.\s*"'
    return bool(re.search(pattern, line))

def find_bracket_placeholders(line: str) -> bool:
    pattern = r'e(?:Log|Info|Warning|Critical|Fatal)\s*\(\s*"[^"]*?\[\s*{[^}]*?}\s*\][^"]*?"'
    return bool(re.search(pattern, line))

def find_spaced_punctuation(line: str) -> bool:
    pattern = r'e(?:Log|Info|Warning|Critical|Fatal)\s*\(\s*"[^"]*?(?:\s-\s|\s:\s)[^"]*?"'
    return bool(re.search(pattern, line, re.DOTALL))

def find_c_str(line: str) -> bool:
    pattern = r'e(?:Log|Info|Warning|Critical|Fatal)\s*\([^)]*\.c_str\(\)'
    return bool(re.search(pattern, line))

def find_caps_words(line: str) -> bool:
    match = re.search(r'e(?:Log|Info|Warning|Critical|Fatal)\s*\(\s*"([^"]+)"', line)
    if not match:
        return False
    
    content = match.group(1)
    words = re.findall(r'\b[A-Z]{2,}\b', content)
    
    allowed = {'ID', 'URL', 'URI', 'IP', 'TCP', 'UDP', 'HTTP', 'HTTPS', 'SSH', 'SSL', 'TLS', 'API', 
               'WS', 'PASS', 'FAIL', 'UPNP', 'TEST', 'JSON', 'VPN'}
    return any(word not in allowed for word in words)

def process_file(file_path: str) -> List[Issue]:
    issues = []
    
    with open(file_path, 'r', encoding='utf-8') as file:
        lines = file.readlines()
        line_idx = 0
        
        while line_idx < len(lines):
            line = lines[line_idx].strip()
            if re.search(r'e(Log|Info|Warning|Critical|Fatal)', line):
                full_line, lines_used = join_multiline_logs(lines, line_idx)
                
                if find_multiple_spaces(full_line):
                    issues.append(Issue(file_path, line_idx + 1, full_line, "Multiple spaces"))
                    
                if find_arg_or_format(full_line):
                    issues.append(Issue(file_path, line_idx + 1, full_line, "Using .arg() or fmt::format"))
                    
                if find_trailing_dot(full_line):
                    issues.append(Issue(file_path, line_idx + 1, full_line, "Trailing dot"))
                    
                if find_bracket_placeholders(full_line):
                    issues.append(Issue(file_path, line_idx + 1, full_line, "Bracket placeholders"))
                    
                if find_caps_words(full_line):
                    issues.append(Issue(file_path, line_idx + 1, full_line, "CAPS LOCK words"))
                    
                if find_spaced_punctuation(full_line):
                    issues.append(Issue(file_path, line_idx + 1, full_line, "Spaced punctuation (: or -)"))
                    
                if find_c_str(full_line):
                    issues.append(Issue(file_path, line_idx + 1, full_line, "Using .c_str() in log argument"))
                
                line_idx += lines_used
            else:
                line_idx += 1
                
    return issues

def main():
    parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    all_issues: List[Issue] = []
    
    for pattern in ['**/*.cpp', '**/*.h']:
        files = glob.glob(os.path.join(parent_dir, pattern), recursive=True)
        for file_path in files:
            file_issues = process_file(file_path)
            all_issues.extend(file_issues)
    
    if all_issues:
        current_file = None
        for issue in sorted(all_issues, key=lambda x: (x.file, x.line_number)):
            if current_file != issue.file:
                current_file = issue.file
                rel_path = os.path.relpath(issue.file, parent_dir)
                print(f"\n{rel_path}:")
            print(f"  Line {issue.line_number}: {issue.issue_type}")
            print(f"    {issue.line}")
            
        print(f"\nTotal issues found: {len(all_issues)}")
    else:
        print("No issues found!")

if __name__ == "__main__":
    main()