import gzip
import shutil
import os

assets = ["app.js", "styles.css", "index.html"]
www_dir = r"c:\Users\nelso\Documents\A_UMNG\ESP32S3 PHYSYS LAB\data\www"

for asset in assets:
    src_path = os.path.join(www_dir, asset)
    dest_path = src_path + ".gz"
    print(f"Compressing {src_path} -> {dest_path}")
    with open(src_path, "rb") as f_in:
        with gzip.open(dest_path, "wb") as f_out:
            shutil.copyfileobj(f_in, f_out)
print("Compression complete!")
