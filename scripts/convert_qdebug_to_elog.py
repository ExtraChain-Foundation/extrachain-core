import re
import glob

def convert_stream_to_fmt(match):
    """Convert Qt stream output to fmt format."""
    parts = re.findall(r'<<\s*([^<;]+?)(?=\s*<<|;|\s*$)', match.group(0))
    if not parts:
        return match.group(0)
    
    format_parts = []
    args = []
    
    for i, part in enumerate(parts):
        part = part.strip()
        if part.startswith('"') and part.endswith('"'):
            format_parts.append(part[1:-1])  # Remove quotes
            if i < len(parts) - 1:  # Add space if not last element
                format_parts.append(" ")
        else:
            format_parts.append("{}")
            args.append(part)
            if i < len(parts) - 1:  # Add space if not last element
                format_parts.append(" ")
    
    log_type = match.group(1)
    fmt_type = {
        'qDebug': 'eLog',
        'qInfo': 'eInfo',
        'qWarning': 'eWarning',
        'qCritical': 'eCritical',
        'qFatal': 'eFatal'
    }.get(log_type, 'eLog')
    
    fmt_string = "".join(format_parts)
    if args:
        return f'{fmt_type}("{fmt_string}", {", ".join(args)});'
    return f'{fmt_type}("{fmt_string}");'

def process_file(file_path):
    with open(file_path, 'r', encoding='utf-8') as file:
        content = file.read()

    stream_pattern = r'(qDebug|qInfo|qWarning|qCritical|qFatal)\(\)(\s*<<[^;]+)+;'
    modified = re.sub(stream_pattern, convert_stream_to_fmt, content)
    
    direct_patterns = [
        (r'qDebug\((.*?)\);', r'eLog(\1);'),
        (r'qInfo\((.*?)\);', r'eInfo(\1);'),
        (r'qWarning\((.*?)\);', r'eWarning(\1);'),
        (r'qCritical\((.*?)\);', r'eCritical(\1);'),
        (r'qFatal\((.*?)\);', r'eFatal(\1);'),
    ]
    
    for old, new in direct_patterns:
        modified = re.sub(old, new, modified, flags=re.MULTILINE)

    if modified != content:
        with open(file_path, 'w', encoding='utf-8') as file:
            file.write(modified)
        return True
    return False

def main():
    source_files = glob.glob('**/*.cpp', recursive=True) + glob.glob('**/*.h', recursive=True)
    modified_count = 0

    for file_path in source_files:
        if process_file(file_path):
            print(f"Modified: {file_path}")
            modified_count += 1

    print(f"\nTotal files modified: {modified_count}")

if __name__ == "__main__":
    main()