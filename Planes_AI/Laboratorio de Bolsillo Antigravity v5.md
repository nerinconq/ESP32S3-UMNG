# **Propuesta Técnica: Laboratorio de Bolsillo implementado por Antigravity v5**

## **Proyecto: Physys ESP32-S3 (Soberanía Tecnológica y Offline-First)**

### **1\. Estrategia de Firmware y Co-creación (C++ & Python) PWA**

Para equilibrar potencia y facilidad de uso en zonas sin internet, utilizaremos un modelo híbrido:

* **Firmware Base (El "Esqueleto"):** Programado en **C++/Arduino Core**. Gestiona el WiFi (Portal Cautivo), Servidor Web, sistema de archivos LittleFS, Watchdog de seguridad y la comunicación asíncrona con Firebase.  
* **Módulo de Usuario (Python):** El estudiante programa su lógica en un script sencillo (sensor\_logic.py).  
  * **Editor Web Integrado:** El Portal Cautivo incluye un editor de texto básico para que el estudiante pegue su código Python directamente desde el navegador del celular.  
* **Seguridad y Watchdog:** Si el código Python del estudiante entra en un bucle infinito o falla, el núcleo en C++ reinicia el proceso y cambia el LED a **Magenta** (Error de Sintaxis/Lógica).  
* **Actualización del Núcleo:** Solo el "Esqueleto" es inamovible para el estudiante, permitiendo que el dispositivo nunca quede inutilizable ("bricked").

### **2\. Gestión de Memoria y Particiones (N16R8: 16MB Flash / 8MB PSRAM)**

Dividiremos el almacenamiento físico para maximizar la autonomía:

* **Partición de Aplicación (6MB):** Almacena el binario del núcleo base (C++).  
* **Partición Web/Sistema (6MB LittleFS):** Contiene la WebApp completa (HTML/JS/uPlot), Service Workers, librerías de Python y recursos gráficos minificados.  
* **Partición de Datos (4MB LittleFS):** Espacio exclusivo para experimentos, archivos de configuración (config.json) y metadatos de los estudiantes.  
* **PSRAM (8MB):** Se utiliza como buffer de alta velocidad para capturas tipo osciloscopio (ej. muestreo de frecuencia de sonido), evitando escrituras constantes en la Flash.

### **3\. Visualización y Ecosistema de Datos**

* **Offline-First:** La WebApp se cachea en el celular mediante **Service Workers** tras el primer contacto. uPlot y Chart.js funcionan sin internet desde el ESP32.  
* **Desmos API:** Se usará solo como fallback online; el sistema prioriza librerías locales para garantizar gráficas fluidas en la selva o el desierto.  
* **Consumo Protegido:** Un interruptor de **"Modo Ahorro"** en la App bloquea cualquier intento de conexión a internet (peticiones externas) mientras el celular no esté en modo "Sincronización", protegiendo el saldo del padre.  
* **Visor de Pines (GPIO Viewer):** Pestaña dedicada para ver y actuar sobre los pines GPIO en tiempo real, facilitando el diagnóstico del hardware.

### **4\. Integración con Bitácora-UMNG y Branding**

* **Branding Local:** El docente puede subir un config.json con el logo de la facultad/universidad y nombre del curso, personalizando la experiencia del estudiante.  
* **Exportación JSON:** El sistema genera archivos compatibles con [bitacora-UMNG](https://nerinconq.github.io/bitacora-UMNG/) para autollenar tablas de datos experimentales.  
* **Sección General:** La WebApp permite poblar los datos generales del informe (Nombre, Fecha, Proyecto) incluso antes de tener internet.

### **5\. Flujo de Trabajo y Sincronización Diferida**

1. **Práctica (Offline):** El estudiante conecta el celular al WiFi "Physys-Lab" \-\> Identificación con ID Institucional \-\> Medición vía WebSockets \-\> Guardado en LittleFS (ESP32) e IndexedDB (Celular).  
2. **Sincronización (Online):** Cuando hay cobertura (meses después), se accede a la "Página en la Nube" suplementaria \-\> Login Firebase \-\> **Subida en Bloque** (se envía un solo paquete comprimido con todos los experimentos del mes para ahorrar datos).  
3. **Seguridad:** Perfiles diferenciados. El estudiante mide y guarda; el docente tiene permisos para limpiar la memoria Flash una vez confirmada la subida a la nube.  
4. **Factory Reset:** Botón físico que, al presionarse por 10s, limpia el ID institucional y archivos de datos para que el kit pase a un nuevo estudiante.

### **6\. Semáforo de Estado (LED RGB)**

Acompañado siempre de un mensaje descriptivo en la pantalla del celular:

* **Blanco Fijo:** Sistema listo / Modo AP activo.  
* **Blanco Parpadeante:** WiFi Activo / Esperando conexión.  
* **Verde Fijo:** Datos exportados a la Bitácora con éxito.  
* **Azul Parpadeante:** Capturando datos (Midiendo).  
* **Verde (3 destellos):** Experimento guardado con éxito en memoria local.  
* **Rojo Parpadeante:** Batería 18650 crítica (\<10%).  
* **Amarillo Parpadeante:** Memoria llena (Requiere sincronizar/exportar).  
* **Cian:** Sincronizando datos con la nube de Firebase.  
* **Magenta:** Error de sintaxis en el código Python del estudiante.

### **Esquema de Entregables para Seguimiento**

| Hito | Entregable | Descripción Técnica |
| :---- | :---- | :---- |
| **H1** | **Núcleo Base C++** | Código Arduino con Servidor Web, Portal Cautivo y particiones 6/6/4 MB. |
| **H2** | **Motor WebApp PWA** | Interfaz HTML/JS con uPlot, Modo Ahorro y exportación JSON para Bitácora. |
| **H3** | **Capa Python Estudiante** | Editor web integrado y 3 ejemplos base (ToF, ADC, I2C) para sensor\_logic.py. |
| **H4** | **GPIO Viewer Embebido** | Pestaña de diagnóstico de pines integrada en la interfaz local. |
| **H5** | **Módulo Cloud Firebase** | Lógica de "Subida en Bloque", Auth de perfiles y TTL de 30 días para datos. |
| **H6** | **Manual de Handover** | Guía para el "Cambio de Mano" y uso del botón de Factory Reset. |

