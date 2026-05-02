# Guía de Instalación - Physys Lab ESP32-S3

Este directorio contiene los archivos mínimos necesarios para compilar e instalar el sistema en un nuevo ESP32-S3.

## Requisitos
1. Tener instalado [Python](https://www.python.org/).
2. Tener instalado [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html).
   - Se instala con: `pip install platformio`

## Pasos para instalar desde Terminal

Abre una terminal (PowerShell o CMD) dentro de esta carpeta y ejecuta:

1. **Subir el Firmware (Código Principal):**
   ```bash
   pio run -t upload
   ```

2. **Subir el Sistema de Archivos (Interfaz Web):**
   ```bash
   pio run -t uploadfs
   ```

3. **Ver el Monitor Serial (Opcional):**
   ```bash
   pio device monitor
   ```

## Notas Importantes
- **Boot + Reset:** Si la carga no inicia automáticamente, mantén presionado el botón **BOOT**, presiona una vez **RESET** y suelta **BOOT**.
- **Particiones:** No borres el archivo `partitions.csv`, es vital para que la interfaz web (16MB) quepa en la memoria.
