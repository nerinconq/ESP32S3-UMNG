# ✅ Checklist de Desarrollo — Physys Lab v9.0

Este documento sirve como hoja de ruta para alcanzar la estabilidad total antes del primer gran "commit" a producción y Git.

## 📦 1. Preparación para Git (Prioridad Alta)
- [x] **Configuración de `.gitignore`** (Completado)
- [x] **Limpieza de Archivos Temporales** (Completado: logs y archivos pesados eliminados)
- [x] **Actualización del README.md** (Completado: v9.0, mDNS, USB Host, Protocolo LED)

## 🔬 2. Refinamiento de Sensores (Lógica de Medición)
- [ ] **Filtrado de Rangos ToF (C++)**: 
  - Implementar lógica para ignorar lecturas fuera de `distMin` y `distMax` en `readSensors()`.
- [ ] **Optimización PSRAM**:
  - Convertir el buffer de `highSpeedBuffer` en un **Buffer Circular** funcional.
- [ ] **Estabilidad del Gatillo**:
  - Validar que el "Auto-Stop" por distancia no interfiera con experimentos de caída libre (ajuste de lógica de dirección).

## 💾 3. USB Host MSC (Estabilidad Híbrida)
- [ ] **Prueba de Escritura Masiva**: Validar que archivos de >1MB se copien correctamente al pendrive sin errores de LittleFS.
- [ ] **Control de Errores de Montaje**: Notificar vía LED (Magenta) si el pendrive no es detectado o es incompatible (FAT32/exFAT).

## 🌐 4. Interfaz Web (UI/UX)
- [ ] **Indicador de Buffer**: Mostrar en la web qué porcentaje de la PSRAM se ha utilizado durante la captura.
- [ ] **Mejora del Exportador CSV**: Asegurar que los saltos de línea sean compatibles con Excel móvil (CRLF).

## ☁️ 5. Cloud & Persistence (H5)
- [ ] **Sincronización Firebase**: Implementar el botón de "Subida a la Nube" para experimentos guardados en IndexedDB.
- [ ] **Validación Offline**: Verificar que el sistema funcione 100% sin internet antes de intentar sincronizar.

---

> [!TIP]
> **Paso 1 Finalizado**: El repositorio está impecable. ¿Quieres que hagamos el commit inicial o pasamos directamente al **Paso 2: Filtrado de Sensores** para pulir el firmware?
