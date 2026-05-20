import os
try:
    from PIL import Image
    import pillow_avif  # Registrar el plugin de AVIF si está instalado
except ImportError:
    pass

def convertir():
    avif_path = r"c:\Users\nelso\Documents\A_UMNG\ESP32S3 PHYSYS LAB\camesp32s3.avif"
    png_path = r"c:\Users\nelso\Documents\A_UMNG\ESP32S3 PHYSYS LAB\camesp32s3.png"
    
    if not os.path.exists(avif_path):
        print("El archivo camesp32s3.avif no existe en la raíz del proyecto.")
        return

    print("Intentando abrir e importar camesp32s3.avif...")
    try:
        # Intentar con Pillow convencional (algunas distribuciones modernas lo soportan)
        im = Image.open(avif_path)
        im.save(png_path, "PNG")
        print("¡Éxito! Imagen convertida y guardada como camesp32s3.png")
    except Exception as e:
        print(f"Error al convertir con Pillow básico: {e}")
        print("Intentando instalar dependencias adicionales si es necesario.")

if __name__ == "__main__":
    convertir()
