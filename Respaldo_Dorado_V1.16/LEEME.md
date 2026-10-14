# Respaldo Dorado – Physys Lab V_1_16_07_26

Esta es la versión estable que se verificó antes de la presentación con colegios: NAU7802 calibrado, VI5300 y doble bus I2C.

- **Firmware compilado:** 7 de agosto de 2026, 21:43 (el mismo que se flasheó y se probó).
- **Paquete armado:** 13 de octubre de 2026.
- **Código fuente equivalente:** tag de Git `v1.16-estable-presentacion` en GitHub.

## Contenido
| Carpeta | Para qué tarjetas |
|---|---|
| `base/` | Base y **Clon** (PSRAM QSPI) |
| `cam/` | **CAM** (PSRAM OPI de 8 MB) |
| `SHA256.txt` | Huellas para comprobar que ningún `.bin` se dañó al copiarlo |

## Cómo regrabar una tarjeta
1. Conecta el ESP32-S3 por USB.
2. Haz doble clic en `flashear_respaldo.bat`.
3. Elige el tipo de tarjeta, el modo y el puerto COM.
   - **Normal:** conserva las calibraciones guardadas.
   - **Rescate:** borra toda la memoria. Después hay que volver a calibrar la celda de carga y reasignar los sensores.

## Cómo volver al código fuente de esta versión
```
git fetch --tags
git checkout v1.16-estable-presentacion
```
Para regresar al desarrollo normal: `git checkout master`.

## Offsets de memoria (para grabar manualmente)
`0x0` bootloader · `0x8000` partitions · `0xd000` boot_app0 · `0x10000` firmware · `0x610000` littlefs (flash de 16 MB, QIO, 80 MHz)
