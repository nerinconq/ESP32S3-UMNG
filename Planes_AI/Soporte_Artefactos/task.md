# Lista de Tareas: Detalles UX y Frecuencia de Muestreo (v9.6)

- `[x]` Tarea 1: Modificar `data/www/index.html` para incorporar el botón `btn-nav-chart` (📊) en `.nav-actions`.
- `[x]` Tarea 2: Modificar `data/www/app.js` para:
  - Añadir soporte de clase activa y click al botón `btn-nav-chart`.
  - Añadir click-redirección en `sensor-select` dropdown.
  - Implementar desenfoque global de selectores (`blur()`) en `change`.
  - Reemplazar `window.open` por un iframe oculto en `serverDownload()`.
- `[x]` Tarea 3: Modificar `src/main.cpp` para:
  - Sincronizar clave NVS en `loadSettings()` (`"rate"` -> `"sample_rate"` tipo `Int`).
  - Reemplazar cuello de botella de `33` ms en `broadcastSensorData()` por `(sampleRateMs - 1)` ms.
- `[x]` Tarea 4: Ejecutar script de compresión `python scratch/compress_assets.py`.
- `[x]` Tarea 5: Verificar compilación en PlatformIO (`pio run -e esp32s3base`).
- `[x]` Tarea 6: Subir archivos con `uploadfs` y `upload` en `COM6`.
- `[x]` Tarea 7: Respaldar archivos actualizados en `Planes_AI/Soporte_Artefactos/`.
