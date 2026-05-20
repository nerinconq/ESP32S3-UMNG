# Plan de Implementación: Corrección de Detalles de UX, Descargas y Frecuencia de Muestreo (v9.6)

Este plan detalla las soluciones técnicas para resolver los cuatro detalles reportados por el Profesor Nelson, garantizando que el sistema sea 100% confiable, fluido y optimizado para estudiantes de escasos recursos en la zona rural, respetando la estructura de seguridad de roles (Docente/Líder/Estudiante) para evitar conflictos.

---

## 🛠️ Detalles a Resolver y Propuesta Técnica

### 📊 Detalle A: Navegación Directa a Medición (Sin Secuencias)
* **Problema:** Si el usuario entra a "Configuración" o "GPIO Viewer", no hay un botón directo para regresar a la pantalla de gráficos y medición. Tienen que presionar secuencialmente los mismos botones para retroceder, o usar un comportamiento de blur inconsistente del selector de sensores.
* **Propuesta:**
  1. Agregar un botón de navegación directo **Medición (📊)** con ID `btn-nav-chart` en `index.html` dentro de la barra `.nav-actions`.
  2. En `app.js` -> `switchTab()`, sincronizar la clase `.active` en `btn-nav-chart` de modo que se ilumine en azul cyan cuando el usuario se encuentre en cualquiera de las pestañas de sensores (`movimiento`, `rotacion`, `fuerza`).
  3. En `app.js` -> `DOMContentLoaded`, asociar al botón `btn-nav-chart` la acción directa de retornar a `lastSensorTab` (o `movimiento` por defecto).
  4. Agregar soporte en `sensor-select` para que si se hace clic/mousedown sobre él estando en config/gpio, regrese instantáneamente a la medición activa.

### 🎯 Detalle B: Scroll Seguro en Desplegables (Sin Alterar Valores)
* **Problema:** En móviles y PC, al seleccionar un valor en un dropdown (como la frecuencia) y luego hacer scroll vertical con el dedo o el scroll del mouse para bajar al botón "Guardar", el navegador interpreta el scroll como un cambio de opción en el selector enfocado, cambiando el valor de forma accidental.
* **Propuesta:**
  * Implementar un manejador de eventos global en la carga del documento (`DOMContentLoaded` de `app.js`) que quite el foco (`.blur()`) a cualquier elemento `<select>` inmediatamente después de que se registre un cambio (`'change'`).
  ```javascript
  document.addEventListener('change', (e) => {
      if (e.target.tagName === 'SELECT') {
          e.target.blur();
      }
  });
  ```
  Esto retira el foco del selector al instante, devolviendo la acción de scroll a la ventana del navegador de manera 100% segura.

### ⬇️ Detalle C: Descarga HTML Segura en Móvil (Sin Expulsión de App)
* **Problema:** Al descargar el visor interactivo en HTML o datos, en algunos navegadores móviles (especialmente en portales cautivos sin datos), `window.open` abre la descarga en la misma pestaña o altera el historial de navegación. Al presionar el botón "Atrás" físico o virtual del celular, el navegador sale de la aplicación web PhysysLab en lugar de solo cerrar la descarga.
* **Propuesta:**
  * Modificar la función `serverDownload()` en `app.js` para utilizar un `<iframe>` oculto e invisible. Al actualizar el atributo `src` del iframe a la URL `/api/temp-export`, el navegador procesa la descarga del archivo (gracias a la cabecera `Content-Disposition: attachment` que envía el ESP32) de forma 100% asíncrona, sin redirigir el historial de la pestaña principal y sin crear estados de navegación fantasmas.
  ```javascript
  let iframe = document.getElementById('download-iframe');
  if (!iframe) {
      iframe = document.createElement('iframe');
      iframe.id = 'download-iframe';
      iframe.style.display = 'none';
      document.body.appendChild(iframe);
  }
  iframe.src = '/api/temp-export';
  ```

### ⚡ Detalle D: Frecuencia de Muestreo Real (Corrección de Firmware)
* **Problema:** Al tomar datos con diferentes frecuencias (ej. 100 Hz vs 10 Hz), el usuario obtuvo el mismo número de muestras, lo cual rompe la lógica física del experimento.
* **Causas y Lógica de Seguridad (Mapeo de Roles):**
  1. **Bottleneck Hardcodeado (Límite de Transmisión):** En `src/main.cpp` -> `broadcastSensorData()`, la restricción fija `if (now - lastBroadcast < 33) return;` limitaba la transmisión por WebSocket a un máximo de ~30 Hz (33 ms). Cualquier frecuencia más rápida se truncaba. Cambiaremos esto a un límite dinámico según la frecuencia seleccionada: `(sampleRateMs - 1)` ms.
  2. **Inconsistencia en la Clave NVS:** `loadSettings()` cargaba con la clave `"rate"` (tipo UInt) y `saveSettings()` guardaba con la clave `"sample_rate"` (tipo Int). Al reiniciar la placa, la frecuencia regresaba a 10 ms (100 Hz). Sincronizaremos ambas a `"sample_rate"` usando `getInt`.
  3. **Seguridad y Control Multiusuario (SE CONSERVA):** Con el fin de evitar que múltiples estudiantes en la misma mesa envíen comandos al mismo tiempo (causando caos e interferencias), **se mantendrá estrictamente intacta la protección de IP de Docente/Líder**. Solo el Líder o Docente autenticado tendrá permisos para cambiar la frecuencia (`SET_RATE`), resetear el encoder (`RESET_ENC`), invertir la dirección del encoder (`INVERT_ENC`) y habilitar el filtro HX (`TOGGLE_HX_FILTER`). Los estudiantes ordinarios podrán visualizar los datos en tiempo real y guardarlos en sus propios celulares.

---

## 🛠️ Proposed Changes

### Componente 1: Firmware Core (`src/main.cpp`)

#### [MODIFY] [main.cpp](file:///c:/Users/nelso/Documents/A_UMNG/ESP32S3%20PHYSYS%20LAB/src/main.cpp)
* **Sincronización NVS (`loadSettings`)**: Cambiar la clave de carga `"rate"` a `"sample_rate"` y usar `getInt` para coincidir con la escritura en `saveSettings`.
* **Eliminar Cuello de Botella en Transmisión (`broadcastSensorData`)**:
  - Reemplazar el límite fijo de `33` ms por `(sampleRateMs - 1)` ms para permitir el flujo real de datos a las frecuencias rápidas configuradas por el Líder o Docente.
* **Seguridad (Retenida)**: No se modifica la protección por IP de los comandos WebSocket. Únicamente se sincronizan los parámetros y se asegura la persistencia en NVS.

---

### Componente 2: Frontend Web (`data/www/`)

#### [MODIFY] [index.html](file:///c:/Users/nelso/Documents/A_UMNG/ESP32S3%20PHYSYS%20LAB/data/www/index.html)
* **Botón de Medición**: Añadir `<button class="nav-icon-btn active" id="btn-nav-chart" title="Medición">📊</button>` en la barra `.nav-actions` de la barra de sensores.

#### [MODIFY] [app.js](file:///c:/Users/nelso/Documents/A_UMNG/ESP32S3%20PHYSYS%20LAB/data/www/app.js)
* **Sincronización en `switchTab()`**: Añadir lógica para alternar la clase `.active` de `btn-nav-chart` cuando el usuario cambie entre pestañas.
* **Evento de Click `btn-nav-chart`**: Agregar el listener para cambiar de pestaña directamente al sensor activo (`lastSensorTab` o `'movimiento'`).
* **Mejora en `sensor-select`**: Permitir que cualquier clic en el selector mientras se está en Config/GPIO redireccione a la pestaña de medición actual.
* **Foco y Desplazamiento (Blur global)**: Agregar el listener global en `DOMContentLoaded` para desenfocar los elementos `<select>` tras un cambio.
* **Descarga Invisible (`serverDownload`)**: Reemplazar `window.open` por un `iframe` oculto que maneje la descarga sin alterar la URL ni la navegación del navegador principal.

---

## 🧪 Verification Plan

### Automated Tests
1. Realizar una compilación de control con PlatformIO local:
   `pio run -e esp32s3base`

### Manual Verification
1. Subir el sistema de archivos (`LittleFS`) y el firmware comprimido mediante PlatformIO:
   `pio run -e esp32s3base -t uploadfs`
   `pio run -e esp32s3base -t upload`
2. **Prueba Detalle A (Navegación):** Entrar a Configuración/GPIO y hacer clic en el botón 📊 en el menú. Regresar instantáneamente a la medición activa.
3. **Prueba Detalle B (Scroll):** Seleccionar una frecuencia en el desplegable e intentar arrastrar (scroll) inmediatamente. El dropdown debe perder el foco y la pantalla debe deslizarse de manera segura.
4. **Prueba Detalle C (Descargas):** Descargar los datos como HTML en el celular. Comprobar que no se altera el historial del navegador y que el botón físico "Atrás" del celular no saca al estudiante de PhysysLab.
5. **Prueba Detalle D (Frecuencia de Muestreo):** Como Docente o Líder, configurar frecuencias a 10 Hz (100 ms) y 100 Hz (10 ms) y medir durante 2 segundos. Confirmar que la toma de 10 Hz da ~20 puntos y la de 100 Hz da ~200 puntos.
