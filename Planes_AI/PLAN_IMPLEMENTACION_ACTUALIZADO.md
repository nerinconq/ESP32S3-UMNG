# 📋 Plan de Implementación Actualizado — Physys Lab

## ✅ Tareas Completadas

### 🌐 Conectividad y Red
- [x] **Implementación de mDNS**: El dispositivo ahora es accesible vía `http://physyslab.local`.
- [x] **Soporte CORS**: Se añadieron encabezados `Access-Control-Allow-Origin: *` para permitir interacciones desde cualquier origen (celulares, tablets, apps externas).
- [x] **Identificación por MAC**: El SSID del AP ahora incluye un sufijo basado en la MAC del dispositivo para evitar colisiones en laboratorios con múltiples ESP32.

### 💾 Almacenamiento y USB
- [x] **USB Host MSC Real**: Se migró del dummy a una implementación funcional basada en el stack nativo de ESP32-S3.
  - [x] Detección automática de pendrives.
  - [x] Montaje en `/usb`.
  - [x] Wrapper para logueo de datos en caliente.

### 🔬 Sensores y Lógica
- [x] **Perfil de Sensores Dinámico**: Actualización de `sensor_logic.py` con la plantilla de configuración Standalone ToF.
- [x] **Lógica de Gatillo (Sample Trigger)**:
  - [x] Implementación de "Zona Muerta" (inicio automático tras movimiento > 2mm).
  - [x] Parada automática al alcanzar la longitud del tubo de montaje.
  - [x] Comandos WebSocket `START_TRIGGER` y `SET_TUBE`.

---

## ⏳ Pendientes (Checklist de Próximos Pasos)

### 🛠️ Interfaz Web (UI)
- [x] **Botón "Toma de Muestra"**: Añadir el botón en el Dashboard para activar el modo gatillo.
- [x] **Configuración de Tubo**: Añadir un input para definir el largo del tubo (mm) desde la web.
- [x] **Indicador de Estado de Gatillo**: Mostrar visualmente cuando el sistema está en "Esperando movimiento...".

### 📊 Análisis y Datos
- [ ] **Validación de Rangos ToF**: Integrar los límites `dist_min` y `dist_max` del perfil Python en el filtrado de datos de C++.
- [ ] **Optimización de PSRAM**: Implementar el buffer circular para capturas de alta velocidad prometidas en el H1.

### ☁️ Cloud (H5)
- [ ] **Sincronización Firebase**: Implementar la subida en bloque (Bulk Upload) para experimentos guardados.

---

## 🚦 Semáforo de Estado Actualizado
- ⚪ **Blanco**: Sistema listo.
- 🟠 **Naranja**: Esperando Gatillo (Toma de muestra activada).
- 🔵 **Azul**: Grabando datos.
- 🟢 **Verde**: Experimento guardado con éxito.
- 🟡 **Amarillo**: Inicializando / Flash llena.
- 🟣 **Magenta**: Error en sensores o script.
