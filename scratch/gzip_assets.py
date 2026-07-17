import gzip
import os

web_dir = "data/www"
files_to_compress = [
    "index.html",
    "app.js",
    "styles.css",
    "manual.html",
    "firebase-sync.js"
]

for fname in files_to_compress:
    src = os.path.join(web_dir, fname)
    dst = src + ".gz"
    if os.path.exists(src):
        with open(src, "rb") as f_in:
            data = f_in.read()
        with gzip.open(dst, "wb", compresslevel=9) as f_out:
            f_out.write(data)
        orig_size = len(data)
        gz_size = os.path.getsize(dst)
        ratio = (1 - gz_size / orig_size) * 100 if orig_size > 0 else 0
        print(f"  {fname}: {orig_size:,} -> {gz_size:,} bytes ({ratio:.1f}% reduccion)")
    else:
        print(f"  {fname}: NO ENCONTRADO")

print("\nCompresion GZIP completada.")
