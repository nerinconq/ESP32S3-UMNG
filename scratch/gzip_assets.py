import gzip
import shutil
import os

files_to_compress = [
    "data/www/index.html",
    "data/www/app.js",
    "data/www/manual.html",
    "data/www/styles.css",
    "data/www/firebase-sync.js"
]

print("Comprimiendo recursos web...")
for file_path in files_to_compress:
    if os.path.exists(file_path):
        gz_path = file_path + ".gz"
        with open(file_path, 'rb') as f_in:
            with gzip.open(gz_path, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)
        orig_size = os.path.getsize(file_path)
        gz_size = os.path.getsize(gz_path)
        reduction = (1 - (gz_size / orig_size)) * 100
        print(f"[OK] {file_path} -> {gz_path} ({orig_size/1024:.1f} KB -> {gz_size/1024:.1f} KB, -{reduction:.1f}%)")
    else:
        print(f"[ERR] Archivo no encontrado: {file_path}")
print("Compresion completa!")
