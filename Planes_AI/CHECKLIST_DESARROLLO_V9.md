# ✅ Checklist de Desarrollo — Physys Lab v9.0

Este documento sirve como hoja de ruta para alcanzar la estabilidad total antes del primer gran "commit" a producción y Git.

## 📦 1. Preparación para Git (Prioridad Alta)
- [x] **Configuración de `.gitignore`** (Completado)
- [x] **Limpieza de Archivos Temporales** (Completado: logs y archivos pesados eliminados)
- [x] **Actualización del README.md** (Completado: v9.0, mDNS, USB Host, Protocolo LED)

## 🔬 2. Refinamiento de Sensores (Lógica de Medición)
- [x] **Filtrado de Rangos ToF (C++)**: 
  - Implementado (Filtro Anti-8190: ignora lecturas >8000 y conserva último valor válido. Commit `93d3407`).
- [ ] **Optimización PSRAM**:
  - Convertir el buffer de `highSpeedBuffer` en un **Buffer Circular** funcional.
- [x] **Estabilidad del Gatillo**:
  - Código implementado: parada 10mm antes de zona muerta (bajada) y en largo del tubo (subida). ⚠️ Pendiente validación en campo.

## 💾 3. USB Host MSC (Estabilidad Híbrida)
- [ ] **Prueba de Escritura Masiva**: Validar que archivos de >1MB se copien correctamente al pendrive sin errores de LittleFS.
- [ ] **Control de Errores de Montaje**: Notificar vía LED (Magenta) si el pendrive no es detectado o es incompatible (FAT32/exFAT).

## 🌐 4. Interfaz Web (UI/UX)
- [ ] **Indicador de Buffer**: Mostrar en la web qué porcentaje de la PSRAM se ha utilizado durante la captura.
- [ ] **Mejora del Exportador CSV**: Asegurar que los saltos de línea sean compatibles con Excel móvil (CRLF).

## ☁️ 5. Cloud & Persistence (H5)
- [ ] **Sincronización Firebase**: Implementar el botón de "Subida a la Nube" para experimentos guardados en IndexedDB.
- [ ] **Validación Offline**: Verificar que el sistema funcione 100% sin internet antes de intentar sincronizar.

## 📊 6. Exportación y Compatibilidad con Desmos
- [x] **Botón "Copiar para Desmos" (TSV)**: Agregar al modal de exportación un botón que copie datos como columnas separadas por tabulación, compatible con Desmos, Excel y Google Sheets.
- [x] **Enlace Directo a Desmos (API)**: Generar un botón "Abrir en Desmos" que lance la calculadora con datos precargados (requiere internet).
- Propuesta visual detallada: `Planes_AI/PROPUESTA_INTEGRACION_DESMOS.html`

---

> [!TIP]
> **Paso 1 Finalizado**: El repositorio está impecable. ¿Quieres que hagamos el commit inicial o pasamos directamente al **Paso 2: Filtrado de Sensores** para pulir el firmware?
