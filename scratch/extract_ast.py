import sys
import json
from graphify.extract import collect_files, extract
from pathlib import Path

detect_path = Path('graphify-out/.graphify_detect.json')
detect = json.loads(detect_path.read_text(encoding='utf-8'))

# Look for code files including .ino
code_files = []
# Scan important directories
search_dirs = ['src', 'data', 'arbol de aprendizaje de código', 'produccion/src']
extensions = ['.cpp', '.h', '.ino', '.py', '.js']

for s_dir in search_dirs:
    path = Path(s_dir)
    if path.exists():
        for ext in extensions:
            for f in path.rglob(f'*{ext}'):
                if 'Freenove' not in str(f) and 'node_modules' not in str(f):
                    code_files.append(f)

print(f"Files: {[str(f) for f in code_files]}")
if code_files:
    result = extract(code_files, cache_root=Path('.'))
    Path('graphify-out/.graphify_ast.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(f'AST: {len(result["nodes"])} nodes, {len(result["edges"])} edges')
else:
    print("No code files found.")
