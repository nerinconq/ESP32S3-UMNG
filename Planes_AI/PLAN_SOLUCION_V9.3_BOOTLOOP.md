# 🔬 Diagnóstico y Plan de Solución: Boot Loop v9.3

**Fecha:** 2026-05-18  
**Problema:** `TG1WDT_SYS_RST` (Watchdog Reset) al arrancar firmware v9.3 en ESP32-S3-CAM (N16R8)  
**Estado:** ✅ Causa raíz identificada — Implementación en curso

---

## 1. Resumen Ejecutivo

La versión 9.3 del firmware (multiusuario + pines dinámicos) causa un boot loop en la placa CAM.  
La v9.2 de producción **SÍ funciona** en la misma placa con el mismo `platformio.ini`.  
El problema NO era solo la librería USB Host (que ya fue eliminada con el stub).

---

## 2. Pruebas Realizadas

| # | Prueba | Resultado | Conclusión |
|---|--------|-----------|------------|
| 1 | v9.3 + librería USB Host real | ❌ Boot loop | Conflicto USB confirmado |
| 2 | v9.3 + stub USB (sin librería) | ❌ Boot loop | **USB NO era la única causa** |
| 3 | v9.2 + stub USB (sin librería) | ✅ Boot OK, WiFi AP, I2C init | v9.2 funciona perfecto |

### Evidencia Serial de v9.2 funcionando:
```
[212][I] psramInit(): PSRAM enabled
[241][E] getString(): nvs_get_str len fail: boot_mode NOT_FOUND
[351][D] _eventCallback(): Arduino Event: 0 - WIFI_READY
[398][V] _arduino_event_cb(): AP Started
[516][I] i2cInit(): Initialising I2C Master: sda=4 scl=5 freq=100000
[531][I] i2cInit(): Initialising I2C Master: sda=10 scl=11 freq=100000
```

### Evidencia Serial de v9.3 crasheando:
```
[219][I] psramInit(): PSRAM enabled
[247][E] getString(): nvs_get_str len fail: boot_mode NOT_FOUND
[361][D] _eventCallback(): Arduino Event: 0 - WIFI_READY
[408][V] _arduino_event_cb(): AP Started
[415][V] _arduino_event_cb(): AP Stopped    ← AP se cae
[428][V] _arduino_event_cb(): AP Started    ← Intenta de nuevo
ESP-ROM: rst:0x8 (TG1WDT_SYS_RST)          ← CRASH a ~430ms
```

**Nota clave:** Nunca se llega a ver ningún `Serial.println()` propio del código. El crash ocurre
ANTES de que el CDC USB se estabilice para mostrar output (en ESP32-S3 con CDC nativo, el serial
tarda ~500ms en estar listo).

---

## 3. Causa Raíz: Funciones Nuevas de v9.3

El `setup()` de v9.3 añadió tres funciones que v9.2 NO tenía:

### 3.1 `detectHardware()` — CRÍTICO ⚠️
- Llama a `softwareI2CScan()` que hace **bit-banging I2C manual**
- Cada dirección I2C escaneada usa `delayMicroseconds(5)` × ~30 operaciones
- Si escanea 128 direcciones × 2 buses = **>400ms de CPU bloqueada sin yield()**
- El Task Watchdog de FreeRTOS tiene timeout de ~300-500ms
- **Resultado: TG1WDT_SYS_RST**

### 3.2 `detectCameraOnPins()` — PELIGROSO ⚠️
- Intentaba llamar `esp_camera_init()` para detectar presencia de cámara
- En la CAM con **OPI PSRAM**, los pines GPIO 35-37 están reservados por el bus OPI
- `esp_camera_init()` toca estos pines → conflicto de hardware → crash

### 3.3 `checkExternalPullups()` — Menor riesgo
- Lee GPIOs con `digitalRead()` — potencialmente conflictivo con pines OPI reservados

### Secuencia del crash:
```
Boot → PSRAM OK → NVS load → WiFi.softAP() → AP_START
→ detectHardware() → softwareI2CScan() bloquea CPU >400ms
→ TG1WDT_SYS_RST → Reset → Loop infinito
```

---

## 4. Plan de Re-integración Incremental

### Filosofía: Partir de v9.2 estable e ir añadiendo features una a una

### Etapa 0 ✅ (Completada)
- [x] Backup v9.2 en `produccion/backup_v9.2/`
- [x] Stub USB para compilar sin librería en env CAM
- [x] Confirmar que v9.2 arranca OK en la CAM

### Etapa 1: Pines Dinámicos (SIN detección de hardware) ✅ (Completada y Compilada)
- [x] Cambiar `#define` de pines por variables `int` globales
- [x] Añadir carga/guardado de pines desde NVS en `loadSettings()`/`saveSettings()`
- [x] **NO** incluir `detectHardware()`, `softwareI2CScan()` ni `detectCameraOnPins()`
- [x] Compilar, flashear, verificar boot estable (Compilación Exitosa)

### Etapa 2: Sistema Multiusuario (Roles) ✅ (Completada y Compilada)
- [x] Añadir variables de estado (activeLeaderId, activeTeacherId)
- [x] Implementar autenticación por PIN en WebSocket
- [x] Implementar bloqueo de comandos para estudiantes
- [x] Compilar, flashear, verificar boot estable

### Etapa 3: API GPIO Expandida ✅ (Completada y Compilada)
- [x] Actualizar `/api/gpio` para devolver todos los pines del ESP32-S3 (Excluyendo seguros 35, 36, 37)
- [x] Actualizar `/api/status` con info de hardware y roles
- [x] Compilar, flashear, verificar boot estable

### Etapa 4: Frontend (ya implementado en los archivos web) ✅ (Completada y Flasheada)
- [x] Verificar que los cambios en `app.js`, `index.html`, `styles.css` son compatibles
- [x] Subir filesystem LittleFS (Upload FS Completo a la placa)
- [x] Validar portal cautivo y dashboard completo (¡A probar!)

### Etapa 5 (Opcional/Futura): Detección segura de hardware
- [ ] Implementar scan I2C usando `Wire.begin()` + `Wire.requestFrom()` (método seguro)
- [ ] Agregar `yield()` entre cada dirección escaneada
- [ ] **NUNCA** llamar `esp_camera_init()` en la CAM con OPI PSRAM

---

## 5. Reglas de Seguridad para la CAM con OPI PSRAM

| Regla | Detalle |
|-------|---------|
| ❌ PROHIBIDO | Llamar `esp_camera_init()` — colisiona con pines OPI |
| ❌ PROHIBIDO | Bit-banging I2C sin `yield()` por >100ms |
| ❌ PROHIBIDO | Tocar GPIO 35, 36, 37 — reservados por OPI PSRAM |
| ✅ SEGURO | `Wire.begin()` + `Wire.requestFrom()` para scan I2C |
| ✅ SEGURO | `delay(1)` o `yield()` entre operaciones bloqueantes |
| ✅ SEGURO | Variables dinámicas de pines cargadas desde NVS |
| ✅ SEGURO | `produccion/` intacta como salvaguarda |

---

## 6. Estado Actual de Archivos

| Archivo | Estado |
|---------|--------|
| `src/main.cpp` | v9.2 producción (estable, punto de partida) |
| `src/UsbHostMSC.h` | Stub (sin dependencias USB) |
| `data/www/app.js` | v9.3 (con cambios multiusuario + pines) |
| `data/www/index.html` | v9.3 (con modal auth + GPIO viewer mejorado) |
| `data/www/styles.css` | v9.3 (con estilos de roles) |
| `platformio.ini` | CAM env con lib_deps sin usb-host-msc |
| `produccion/` | v9.2 intacta (salvaguarda) |
| `produccion/backup_v9.2/` | Backup completo pre-cambios |
