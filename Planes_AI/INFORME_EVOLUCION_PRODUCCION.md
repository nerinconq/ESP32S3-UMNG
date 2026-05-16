# 🔍 Comparación: `produccion/` vs Estado Actual (DEV)

**Fecha de análisis:** 2026-05-13 07:25 COT
**Versión Global:** `v9.0` (Sincronizada)

---

## Resultado: ✅ Producción está TOTALMENTE ACTUALIZADA

La carpeta `produccion/` ha sido sincronizada con el estado actual del desarrollo (**v9.0**). Todos los archivos críticos coinciden en tamaño y contenido.

### 📋 Tabla Comparativa de Archivos

| Archivo | Tamaño (PROD) | Tamaño (DEV) | Estado | Notas |
| :--- | :--- | :--- | :--- | :--- |
| `src/main.cpp` | 43,672 bytes | 43,672 bytes | ✅ | Identidad 1:1 (v9.0) |
| `data/www/index.html` | 20,465 bytes | 20,465 bytes | ✅ | UI Simplificada |
| `data/www/app.js` | 63,764 bytes | 63,764 bytes | ✅ | Lógica v9.0 |
| `data/www/styles.css` | 24,887 bytes | 24,887 bytes | ✅ | Estilos Premium |
| `src/UsbHostMSC.h` | 3,102 bytes | 3,102 bytes | ✅ | Stack Nativo IDF |
| `platformio.ini` | 983 bytes | 983 bytes | ✅ | Configuración QIO |
| `partitions.csv` | 272 bytes | 272 bytes | ✅ | LittleFS 8MB |

---

## 🚀 Funcionalidades INTEGRADAS en Producción

### 1. 🏷️ Versión v9.0
- **PROD:** Firmware reporta `v9.0` , UI muestra `v9.0`
- **DEV:** Firmware reporta `v9.0` , UI muestra `v9.0`
- *Estado:* Sincronizado.

### 2. 🌐 Navegación Simplificada
- Reemplazo de pestañas por menú dropdown de sensores.
- Implementación de botones de acceso rápido (⚙️ Config, 📌 GPIO).
- *Estado:* Funcional en producción.

### 3. 💾 Almacenamiento USB Nativo
- Implementación de `msc_host.h` para montaje de pendrives en caliente.
- *Estado:* Verificado y portado.

### 4. ⚡ Estabilidad I2C
- Separación física de buses para ToF y Encoder.
- *Estado:* Implementado en el esquema de pines.

---

## 🔄 Actualización: v9.1 — Filtro Anti-8190

**Fecha:** 2026-05-14 07:05 COT
**Commit:** `93d3407`

### Cambio aplicado
- **`src/main.cpp`**: Corrección del filtro de lecturas erróneas del sensor ToF. Cuando el sensor devuelve >8000 (señal perdida), ahora se **ignora la lectura** y se conserva el último valor válido, en lugar de forzar un salto a `distMax` que generaba escalones en la gráfica.
- **Modo Auto**: Parada de seguridad intacta (verifica valor crudo `dist >= 8000`).
- **Modo Manual**: Sin afectación (aislado por `triggerEnabled = false`).

### Archivos sincronizados a `produccion/`

| Archivo | Estado |
| :--- | :--- |
| `src/main.cpp` | ✅ Actualizado (filtro anti-8190) |
| `data/www/app.js` | ✅ Actualizado |
| `data/www/index.html` | ✅ Actualizado |
| `data/www/styles.css` | ✅ Actualizado |

---

## 🔄 Actualización: v9.2 — Integración Desmos Offline y Estabilidad de Sensores

**Fecha:** 2026-05-15 21:25 COT (Aprox.)
**Commits:** `88d4665`, `52cec5c`

### Avances Obtenidos
1. **Bypass de Portal Cautivo en Android**:
   - Android bloquea silenciosamente las descargas generadas localmente (`blob:` URLs) cuando está conectado a una red WiFi sin salida a internet (como la del ESP32).
   - **Solución implementada**: Se migró la descarga a un flujo de servidor HTTP real. La App (`app.js`) genera los datos y hace un `POST` al nuevo endpoint `/api/temp-export` en el ESP32 (guardando temporalmente en RAM). Luego, se fuerza al navegador a hacer un `GET` a esa misma ruta. El ESP32 responde con la cabecera `Content-Disposition: attachment`, engañando al administrador de descargas de Android para guardar correctamente los archivos `.desmos` y `.csv` en la carpeta física de *Descargas* del celular.
   - Se confirmó que los datos llegan íntegros y son visibles en Google Sheets.

2. **Estabilización de Hardware y Memoria (PSRAM)**:
   - Se identificó y resolvió una falla masiva en los sensores (lecturas en 0 mm y pérdida de I2C) originada por un cambio experimental previo en la inicialización de la PSRAM.
   - **Solución implementada**: Se revirtió la configuración del `platformio.ini` de memoria `qio_opi` nuevamente al estado base `qio_qspi` (commit `52cec5c`). Esto restauró instantáneamente la capacidad del ESP32 de asignar búferes y recolectar telemetría sin interrupciones.

### Estado frente a `produccion/`
**Estado: ✅ TOTALMENTE SINCRONIZADO (2026-05-15 21:28 COT)**
- `produccion/main.cpp` (Actualizado con endpoints API REST)
- `produccion/app.js` (Actualizado con lógica serverDownload)
- `produccion/platformio.ini` (Actualizado a qio_qspi)
- `produccion/index.html` (Actualizado)
- `produccion/styles.css` (Actualizado)

---

> [!TIP]
> **Respaldo Seguro:** La carpeta `produccion/` ahora sirve como un snapshot perfecto para despliegues en campo. Se recomienda no modificarla hasta el próximo hito validado.

---
*Este informe ha sido generado para mantener la trazabilidad de la evolución del proyecto UMNG Physys Lab.*
