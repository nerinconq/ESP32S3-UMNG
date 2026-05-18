import os
import shutil

src_files = [
    ("src/main.cpp", "produccion/src/main.cpp"),
    ("src/UsbHostMSC.h", "produccion/src/UsbHostMSC.h"),
    ("platformio.ini", "produccion/platformio.ini"),
    ("partitions.csv", "produccion/partitions.csv")
]

print("Sincronizando archivos de desarrollo a carpeta de produccion/...")

# Asegurar directorios de destino
os.makedirs("produccion/src", exist_ok=True)
os.makedirs("produccion/data/www", exist_ok=True)

# Copiar archivos principales
for src, dst in src_files:
    if os.path.exists(src):
        shutil.copy2(src, dst)
        print(f"[OK] Copiado: {src} -> {dst}")
    else:
        print(f"[WARN] No encontrado: {src}")

# Copiar directorio de datos
data_src = "data"
data_dst = "produccion/data"

if os.path.exists(data_src):
    # Limpiar destino data anterior si existe
    if os.path.exists(data_dst):
        shutil.rmtree(data_dst)
    shutil.copytree(data_src, data_dst)
    print(f"[OK] Sincronizada carpeta data completa -> {data_dst}")

print("Sincronizacion de produccion finalizada con exito!")
