# Plan UX: Simplificación de Interfaz — Physys Lab v9.0

## Feedback recibido y acciones

| # | Feedback | Acción | Estado |
|---|----------|--------|--------|
| 1 | Demasiadas pestañas, no atractivo | Reemplazar tabs por dropdown de sensor | ✅ |
| 2 | Python en standby | Ocultar pestaña Python del menú | ✅ |
| 3 | Botones "Sincronizado" y "Guardar" confusos | Eliminar, integrar guardado en exportar | ✅ |
| 4 | USB Export debería estar en exportar, no en config | Mover USB Export al flujo de exportación (modal) | ✅ |
| 5 | Demasiados botones de reinicio en config | Un solo botón "Guardar Config" que aplique todo | ✅ |
| 6 | Indicador de batería 18650 | Leer ADC del voltaje y mostrar badge | ⬜ (badge listo, falta C++) |
| 7 | Círculo = semáforo LED + logo institucional | Doble función: logo + indicador de estado | ⬜ |

## Cambios realizados (v9.0)

### HTML (index.html)
- Reemplazado `<nav class="tabs-nav">` (6 tabs) → `<nav class="sensor-bar">` (dropdown + 2 iconos)
- Eliminado botón ⚡ (modo ahorro confuso) y badge "Offline"
- Agregado badge de batería 🔋 (placeholder para ADC)
- Config consolidado: 1 solo botón "Guardar Configuración" (aplica ToF + Freq + Tubo)
- USB Export removido de config (ahora en modal de exportación)
- Eliminado botón duplicado "Reiniciar ESP32"
- Agregado botón ✕ para cerrar panel de config

### JS (app.js)
- `switchTab()` reescrito para sincronizar dropdown + iconos nav
- `saveAllConfig()` nueva función que envía ToF + Rate + Tube en un solo click
- `openExportModal()` nuevo modal unificado: CSV, JSON, Compartir, USB Pendrive
- Init actualizado para sensor dropdown + nav icon listeners
- Version bumped a v9.0

### CSS (styles.css)
- Estilos `.sensor-bar`, `.sensor-dropdown`, `.nav-icon-btn` (dropdown premium con flecha SVG)
- Estilos `.battery-badge`, `.btn-close-config`
- Tabs originales preservados pero ya no se usan

### Firmware (main.cpp)
- Version endpoint actualizado a v9.0

## Orden de implementación (restante)

1. ~~**Paso 1** (HTML): Dropdown sensor + ocultar Python + limpiar botones config~~ ✅
2. ~~**Paso 2** (JS): Lógica del dropdown + USB export en modal de exportar~~ ✅
3. **Paso 3** (C++): ADC para batería 18650 — Pendiente
4. **Paso 4** (HTML+CSS): Círculo semáforo + logo institucional — Pendiente
5. **Paso 5**: Commit + push — Pendiente (esperando validación de cambios)
