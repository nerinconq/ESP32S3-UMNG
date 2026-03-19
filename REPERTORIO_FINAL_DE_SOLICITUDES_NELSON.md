# REPERTORIO FINAL DE SOLICITUDES Y RECOMENDACIONES TÉCNICAS
## Proyecto: Physys Lab S3 - Nelson (UMNG)

Este documento contiene el listado íntegro y detallado de todas las peticiones, especificaciones y enlaces proporcionados por el usuario durante esta sesión de trabajo para su revisión y uso por parte del equipo de desarrollo.

### 1. Especificaciones de Hardware (Chip N16R8)
*   **Modelo de Chip**: ESP32-S3 WROOM-1 (Verificar prioridad de WROOM-1 sobre WROOM-2).
*   **Modo de Memoria Flash**: 16MB en modo **QIO** (instrucción enfática de Nelson).
*   **Modo de PSRAM**: 8MB Octal (o verificar compatibilidad Quad/QIO si es WROOM-1).
*   **Uso de la PSRAM**: Reservada como buffer de alta velocidad para muestreo de sensores (tipo osciloscopio) para evitar escrituras constantes en Flash y proteger la vida útil del silicio.
*   **Modo de Boot**: El sistema NO debe requerir la combinación física BOOT + RESET para flasheos normales, excepto en actualizaciones vía OTA. El firmware actual interfiere con el reseteo automático.

### 2. Esquema de Particionamiento (16MB Totales)
Se solicitó dividir el almacenamiento físico de la siguiente manera para maximizar la autonomía y organización:
*   **Partición de Aplicación (6MB)**: Destinada al binario del núcleo base desarrollado en C++.
*   **Partición Web/Sistema (6MB LittleFS)**: Reservada para la WebApp completa, incluyendo HTML/JS/uPlot, Service Workers, librerías de Python y recursos gráficos minificados.
*   **Partición de Datos (4MB LittleFS)**: Espacio exclusivo para el almacenamiento de experimentos, archivos de configuración `config.json` y metadatos generados por los estudiantes.

### 3. Conectividad y Estabilidad
*   **Red WiFi**: SSID "Physys-Lab". Debe ser estable y visible de inmediato tras el arranque.
*   **Modo Híbrido**: El dispositivo debe alimentarse de su propia red AP, pero tener capacidad de "nutrirse" de redes con internet para actualizaciones y optimizaciones automáticas cuando no esté en zonas aisladas.
*   **Estabilidad Eléctrica**: Resolver el "sonido intermitente" (indicador de bootloop/brownout) sin sacrificar arbitrariamente la potencia de transmisión a menos que sea estrictamente necesario por hardware.

### 4. Dashboards y Visualización (PWA v6)
*   **Referencia Estética**: Réplica de alta fidelidad del diseño Gemini v6 compartido: [Enlace de Referencia Gemini](https://gemini.google.com/share/feb65e8057ee).
*   **Funcionalidades de Gráfica**:
    *   En cada gráfica, el usuario debe poder elegir entre 3 variables: **Posición vs Tiempo**, **Rapidez vs Tiempo** y **Aceleración vs Tiempo**.
    *   **Sensor de Fuerza**: Debe incluir métricas para **Masa**, **Peso Vertical** y **Fuerza** (Horizontal).
*   **Monitor de Pines**: Implementar la visualización de hardware basada en el contenido de la carpeta `Freenove_Ultimate_Starter_Kit_for_ESP32_S3-main` integrada en el proyecto.
*   **Capa Estudiante**: Implementar una arquitectura que permita cargar y ejecutar lógica de sensores vía Python (capa de abstracción).

### 5. Enlaces y Herramientas Recomendadas (MCP Market)
El usuario proporcionó los siguientes enlaces de soporte técnico para la implementación:
*   **Depurador de Firmware**: [ESP32 Firmware Debugger](https://mcpmarket.com/es/tools/skills/esp32-firmware-debugger)
*   **Constructor BOX-3**: [ESP32-S3 BOX-3 Builder](https://mcpmarket.com/es/tools/skills/esp32-s3-box-3-builder)
*   **Servidor MCP ESP32**: [ESP32MCPServer (Github)](https://github.com/navado/ESP32MCPServer)
*   **Servidor Serial**: [Serial MCP Server](https://github.com/adancurusul/serial-mcp-server)
*   **Kit de Inicio Freenove**: [Freenove Ultimate Starter Kit (Github)](https://github.com/Freenove/Freenove_Ultimate_Starter_Kit_for_ESP32_S3)

### 6. Descartes Explícitos
*   **NotebookLM**: No se requiere su integración funcional en este proyecto.
*   **Gestión de Energía**: No se solicitó explícitamente limitar la potencia; se prefiere estabilidad natural del hardware.
*   **GPIO Viewer**: Solo si es la versión actualizada y compatible; de lo contrario, omitir por fallos de núcleo.

---
*Fin del registro de solicitudes solicitadas por Nelson el 11 de Marzo de 2026.*
