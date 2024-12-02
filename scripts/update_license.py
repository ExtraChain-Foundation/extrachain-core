import os
import re
from datetime import datetime

PROJECT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

LICENSE_TEMPLATE = """/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
"""

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    if not content.strip().startswith('/*'):
        new_content = LICENSE_TEMPLATE + content
        needs_update = True
    else:
        new_content = re.sub(r'Copyright \(C\) \d{4}', 'Copyright (C) 2025', content)
        new_content = re.sub(r'<.*?@.*?>', '<official@extrachain.io>', new_content)
        needs_update = new_content != content
    
    if needs_update:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)
        return True
    return False

def main():
    file_count = updated_count = 0
    for root, _, files in os.walk(PROJECT_DIR):
        for file in files:
            if file.endswith(('.cpp', '.h', '.hpp')):
                filepath = os.path.join(root, file)
                file_count += 1
                if process_file(filepath):
                    updated_count += 1
                    print(f"Updated: {filepath}")
    
    print(f"\nProcessed {file_count} files")
    print(f"Updated {updated_count} files")

if __name__ == '__main__':
    main()