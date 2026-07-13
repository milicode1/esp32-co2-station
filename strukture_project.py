import os
import argparse
from pathlib import Path

def print_tree(directory, prefix="", ignore_dirs=None, max_depth=None, current_depth=0):
    """
    Рекурсивно выводит древовидную структуру директории
    
    Args:
        directory: путь к директории
        prefix: префикс для отступов
        ignore_dirs: список директорий для игнорирования
        max_depth: максимальная глубина обхода
        current_depth: текущая глубина
    """
    if ignore_dirs is None:
        ignore_dirs = ['.git', '__pycache__', '.venv', 'venv', 'node_modules', '.idea', '.vscode']
    
    if max_depth is not None and current_depth >= max_depth:
        return
    
    try:
        items = sorted(os.listdir(directory))
        items = [item for item in items if item not in ignore_dirs]
    except PermissionError:
        print(f"{prefix}└── [Permission Denied]")
        return
    except FileNotFoundError:
        print(f"Directory not found: {directory}")
        return
    
    for i, item in enumerate(items):
        path = os.path.join(directory, item)
        is_last = i == len(items) - 1
        
        # Определяем префикс для текущего элемента
        if is_last:
            current_prefix = "└── "
            next_prefix = "    "
        else:
            current_prefix = "├── "
            next_prefix = "│   "
        
        # Выводим текущий элемент
        if os.path.isdir(path):
            print(f"{prefix}{current_prefix}📁 {item}/")
            print_tree(path, prefix + next_prefix, ignore_dirs, max_depth, current_depth + 1)
        else:
            # Добавляем иконки для разных типов файлов
            icon = get_file_icon(item)
            print(f"{prefix}{current_prefix}{icon} {item}")

def get_file_icon(filename):
    """Возвращает иконку в зависимости от расширения файла"""
    ext = filename.lower().split('.')[-1] if '.' in filename else ''
    
    icons = {
        'py': '🐍',
        'js': '📜',
        'html': '🌐',
        'css': '🎨',
        'json': '📋',
        'md': '📝',
        'txt': '📄',
        'yml': '⚙️',
        'yaml': '⚙️',
        'toml': '⚙️',
        'cfg': '⚙️',
        'ini': '⚙️',
        'sh': '💻',
        'bat': '💻',
        'sql': '🗄️',
        'db': '🗄️',
        'jpg': '🖼️',
        'jpeg': '🖼️',
        'png': '🖼️',
        'gif': '🖼️',
        'svg': '🖼️',
        'pdf': '📕',
        'zip': '📦',
        'tar': '📦',
        'gz': '📦',
        'exe': '⚡',
        'dll': '🔧',
        'so': '🔧',
    }
    
    return icons.get(ext, '📄')

def count_files_and_dirs(directory, ignore_dirs=None):
    """Подсчитывает количество файлов и папок"""
    if ignore_dirs is None:
        ignore_dirs = ['.git', '__pycache__', '.venv', 'venv', 'node_modules']
    
    file_count = 0
    dir_count = 0
    
    for root, dirs, files in os.walk(directory):
        # Игнорируем указанные директории
        dirs[:] = [d for d in dirs if d not in ignore_dirs]
        file_count += len(files)
        dir_count += len(dirs)
    
    return file_count, dir_count

def main():
    parser = argparse.ArgumentParser(description='Отображение структуры проекта в виде дерева')
    parser.add_argument('directory', nargs='?', default='.', 
                       help='Путь к директории (по умолчанию текущая)')
    parser.add_argument('--depth', '-d', type=int, 
                       help='Максимальная глубина обхода')
    parser.add_argument('--all', '-a', action='store_true', 
                       help='Показывать скрытые файлы и папки')
    parser.add_argument('--stats', '-s', action='store_true', 
                       help='Показать статистику')
    
    args = parser.parse_args()
    
    ignore_dirs = ['.git', '__pycache__', '.venv', 'venv', 'node_modules', '.idea', '.vscode']
    
    if args.all:
        ignore_dirs = ['__pycache__']  # Оставляем только служебные папки Python
    
    dir_path = args.directory
    
    # Выводим заголовок
    print(f"\n{'='*50}")
    print(f"📂 Структура проекта: {os.path.abspath(dir_path)}")
    print('='*50)
    
    # Выводим дерево
    print(f"📁 {os.path.basename(os.path.abspath(dir_path))}/")
    print_tree(dir_path, ignore_dirs=ignore_dirs, max_depth=args.depth)
    
    # Статистика
    if args.stats:
        files, dirs = count_files_and_dirs(dir_path, ignore_dirs)
        print(f"\n{'='*50}")
        print(f"📊 Статистика:")
        print(f"   📁 Папок: {dirs}")
        print(f"   📄 Файлов: {files}")
        print(f"   📦 Всего элементов: {files + dirs}")
        print('='*50)

if __name__ == "__main__":
    main()
