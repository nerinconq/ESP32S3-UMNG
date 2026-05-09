# H5: Sincronización Firebase

## Contexto
El ESP32 crea una red WiFi AP **sin internet**. La sincronización con Firebase solo puede ocurrir cuando el estudiante reconecta su teléfono a WiFi/datos normales.

## Arquitectura

```
[ESP32 AP] → [Phone PWA] → [IndexedDB local] → [Firestore Cloud]
  sin internet     cache offline       sync cuando hay internet
```

## Archivos a modificar

> [!IMPORTANT]
> **NO se modifica `main.cpp`** — Solo cambios en la webapp.

### [NEW] `data/www/firebase-sync.js` (~120 líneas)
- Módulo de sincronización autónomo.
- **IndexedDB**: Almacena experimentos localmente.
- **Firestore sync**: Sube experimentos pendientes cuando detecta internet.
- **Online/offline detection**: Usa `navigator.onLine` + escucha eventos.
- Usa Firebase JS SDK v10 (modular, CDN cuando hay internet).

### [MODIFY] `data/www/index.html`
- Agrega `<script>` para `firebase-sync.js` (carga después de `app.js`).
- Agrega indicador de sync en el header (badge ☁️ verde/gris).

### [MODIFY] `data/www/app.js`
- Conecta `exportJSON()` con `saveToIndexedDB()`.
- Agrega función `syncToCloud()` llamada cuando el phone detecta internet.

### [MODIFY] `data/www/styles.css`
- Agrega estilos para el badge de sync (mínimo, ~10 líneas).

## Proyecto Firebase
- Se creará un proyecto nuevo: `physys-lab-umng`
- Servicio: **Firestore** (base de datos NoSQL)
- Autenticación: **Anónima** (sin login para estudiantes)
- Reglas: Permitir escritura autenticada, lectura autenticada.

## Verificación
- Compilar y subir filesystem actualizado.
- Verificar que tabs existentes (H1-H4, H6) no se afecten.
