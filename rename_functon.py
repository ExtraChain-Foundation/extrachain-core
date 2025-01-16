import sys
import os
import json
import subprocess
from pathlib import Path
from typing import List, Set, Dict
from dataclasses import dataclass
from clang.cindex import Index, CursorKind, TranslationUnit

@dataclass
class RenameInfo:
    name: str
    new_name: str
    file: Path
    line: int
    kind: str

EXCLUDED_FUNCTIONS = {'qHash', 'eDebug', 'eInfo', 'eWarning', 'eCritical', 'eFatal', 'eSuccess', 'eLog'}
EXCLUDED_CLASSES = {'VariantModel'}
BUILD_DIR = 'build'

def setup_clang():
    # Настройка пути к libclang на Windows
    if sys.platform == "win32":
        possible_paths = [
            r"C:\Program Files\LLVM\bin\libclang.dll",
            r"C:\msys64\mingw64\bin\libclang.dll",
        ]
        for path in possible_paths:
            if os.path.exists(path):
                Config.set_library_file(path)
                break

def generate_compilation_database(project_root: Path):
    build_path = project_root / BUILD_DIR
    if not build_path.exists():
        build_path.mkdir()
    
    print("Generating compilation database...")
    cmake_args = [
        'cmake',
        '..',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
        f'-DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64',
        f'-DQT_QMAKE_EXECUTABLE=C:/Qt/6.8.0/msvc2022_64/bin/qmake.exe',
        '-DCMAKE_C_COMPILER=cl.exe',
        '-DCMAKE_CXX_COMPILER=cl.exe',
        '-DCMAKE_TOOLCHAIN_FILE=E:/ExC/vcpkg/scripts/buildsystems/vcpkg.cmake',
        '-DVCPKG_TARGET_TRIPLET=x64-windows',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
        '-DCMAKE_BUILD_TYPE=Release'
    ]
    
    try:
        result = subprocess.run(
            cmake_args,
            cwd=build_path,
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                'VCPKG_DEFAULT_TRIPLET': 'x64-windows',
                'VCPKG_ROOT': 'E:/ExC/vcpkg'
            }
        )
        print("CMake output:")
        print(result.stdout)
        return build_path / 'compile_commands.json'
    except subprocess.CalledProcessError as e:
        print("CMake configuration failed:")
        print(e.stderr)
        print("\nCMake output before error:")
        print(e.stdout)
        raise

def to_snake_case(name: str) -> str:
    if name.startswith('m_'):
        name = name[2:]
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

def is_qt_type(cursor) -> bool:
    if cursor.type:
        type_str = cursor.type.spelling
        return type_str.startswith('Q') or 'Qt::' in type_str
    return False

def is_our_file(file_path: str, project_root: Path) -> bool:
    path = Path(file_path)
    return (project_root in Path(file_path).parents and
            not any(x in str(path) for x in {'external', 'third_party', 'vendor', BUILD_DIR}))

def should_rename_cursor(cursor, project_root: Path) -> bool:
    # Проверяем базовые условия
    if not cursor.spelling:
        return False
        
    if (cursor.spelling.startswith('Q') or 
        cursor.spelling in EXCLUDED_FUNCTIONS or
        cursor.spelling[0].isupper()):
        return False

    # Проверяем локацию
    if not cursor.location.file:
        return False
    
    if not is_our_file(cursor.location.file.name, project_root):
        return False

    # Проверяем контекст
    if cursor.semantic_parent:
        parent = cursor.semantic_parent
        # Пропускаем Qt классы
        if parent.spelling.startswith('Q'):
            return False
        # Пропускаем исключенные классы
        if parent.spelling in EXCLUDED_CLASSES:
            return False

    # Проверяем специфичные случаи
    if cursor.kind == CursorKind.FIELD_DECL:
        # Для полей класса смотрим на родительский класс
        parent = cursor.semantic_parent
        return not (parent.spelling.startswith('Q') or parent.spelling in EXCLUDED_CLASSES)
    
    # Игнорируем макросы и препроцессор
    if cursor.kind == CursorKind.MACRO_DEFINITION:
        return False
    
    # Игнорируем Qt slots и signals
    if cursor.kind == CursorKind.CXX_METHOD:
        tokens = [t.spelling for t in cursor.get_tokens()]
        if 'slots' in tokens or 'signals' in tokens or 'Q_SLOT' in tokens:
            return False
    
    return True

def process_ast(cursor, project_root: Path, renames: Dict[str, str], is_class_member: bool = False):
    """Рекурсивно обрабатываем AST"""
    if cursor.kind == CursorKind.CLASS_DECL:
        # Обрабатываем члены класса
        for child in cursor.get_children():
            if child.kind == CursorKind.FIELD_DECL:
                process_ast(child, project_root, renames, True)
            else:
                process_ast(child, project_root, renames, False)
    else:
        if should_rename_cursor(cursor, project_root):
            new_name = to_snake_case(cursor.spelling)
            if is_class_member:
                new_name += '_'
            if new_name != cursor.spelling:
                renames[cursor.spelling] = new_name

    for child in cursor.get_children():
        process_ast(child, project_root, renames, is_class_member)

def apply_renames(file_path: Path, renames: Dict[str, str]) -> None:
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()

        lines = content.split('\n')
        new_lines = []
        in_macro = False
        in_comment = False
        
        for line in lines:
            # Пропускаем макросы и препроцессор
            if line.strip().startswith('#'):
                new_lines.append(line)
                in_macro = '\\' in line.rstrip()
                continue
            if in_macro:
                new_lines.append(line)
                in_macro = '\\' in line.rstrip()
                continue

            if '/*' in line:
                in_comment = True
            if '*/' in line:
                in_comment = False

            if not in_comment and not line.strip().startswith('//'):
                # Разбиваем на части, сохраняя строковые литералы
                parts = re.split(r'("(?:[^"\\]|\\.)*")', line)
                for i in range(0, len(parts), 2):
                    for old_name, new_name in sorted(renames.items(), key=lambda x: len(x[0]), reverse=True):
                        parts[i] = re.sub(fr'\b{old_name}\b', new_name, parts[i])
                line = ''.join(parts)

            new_lines.append(line)

        new_content = '\n'.join(new_lines)
        if new_content != content:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(new_content)
            print(f"Updated {file_path}")

    except Exception as e:
        print(f"Error applying renames to {file_path}: {e}")

def main():
    if len(sys.argv) != 2:
        print("Usage: python rename.py <project_directory>")
        sys.exit(1)

    project_root = Path(sys.argv[1]).resolve()
    
    # Генерируем compilation database
    compile_commands = generate_compilation_database(project_root)
    
    with open(compile_commands) as f:
        compile_db = json.load(f)

    # Инициализируем libclang
    setup_clang()
    index = Index.create()

    renames = {}
    
    print("\nAnalyzing files...")
    for entry in compile_db:
        file = Path(entry['file'])
        if not is_our_file(str(file), project_root):
            continue
            
        print(f"Processing {file}")
        
        # Парсим файл с помощью libclang
        try:
            tu = index.parse(
                str(file),
                args=['-x', 'c++'] + entry['command'].split()[1:],
                options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
            )
            
            if tu:
                process_ast(tu.cursor, project_root, renames)
            else:
                print(f"Failed to parse {file}")
                
        except Exception as e:
            print(f"Error processing {file}: {e}")

    if renames:
        print("\nFound symbols to rename:")
        for old_name, new_name in renames.items():
            print(f"{old_name} -> {new_name}")
            
        if input("\nProceed with renaming? (y/n): ").lower() == 'y':
            print("\nApplying renames...")
            
            # Собираем все файлы проекта
            project_files = [
                p for p in project_root.rglob('*')
                if p.suffix in {'.h', '.hpp', '.cpp'} and 
                not any(x in str(p) for x in {'external', 'third_party', 'vendor', BUILD_DIR})
            ]
            
            for file in project_files:
                apply_renames(file, renames)
                
            print("Done!")
    else:
        print("No symbols to rename found")

if __name__ == '__main__':
    main()