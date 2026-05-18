Cuando carga por primera vez sale con un conjunto de pines# Guía de Selección y Restricción de Pines (ESP32-S3 y ESP32-S3-CAM)

Este documento justifica la lista de **Pines Seguros (SAFE_PINS)** que pueden ser asignados dinámicamente desde el portal cautivo del Physys Lab (v9.3), así como los tres perfiles (presets) preestablecidos.

## 1. Pines Disponibles (SAFE_PINS)

En la placa base ESP32-S3, la lista de pines seleccionables en la interfaz es:
`[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 26, 35, 36, 37, 38, 39, 40, 41, 42, 45, 46, 47]`

### Exclusiones Críticas por Hardware (Por qué NO están todos)

* **GPIO 0:** Usado para el botón BOOT (Pull-up interno).
* **GPIO 19, 20:** Usados para el controlador nativo USB (USB-OTG D- y D+).
* **GPIO 22, 23, 24, 25:** Reservados estrictamente para el bus SPI de la memoria Flash.
* **GPIO 27, 28, 29, 30, 31, 32, 33, 34:** Reservados internamente (y en algunas revisiones de placa, conectados a SPI Flash/RAM OPI en placas con Quad/Octal SPI).
* **GPIO 43, 44:** Pines por defecto de UART TX y RX (Serial).
* **GPIO 48:** Usado a menudo para el LED RGB NeoPixel (WS2812).

---

## 2. Perfiles Preestablecidos (Presets)

### A. Perfil Básico (ESP32-S3 Base - Sin Cámara)

Este es el perfil original usado en los laboratorios donde no hay cámara conectada. Los pines se agrupan por conveniencia física (vecindad de headers).

* **ToF SDA / SCL:** 4 / 5
* **Encoder SDA / SCL:** 10 / 11
* **HX711 DT / SCK:** 6 / 7

### B. Perfil Cámara (ESP32-S3-CAM amigable)

Las placas tipo ESP32-S3-CAM (especialmente las Freenove o genéricas WROOM-1 N16R8) tienen el bus de la cámara y de la tarjeta SD físicamente cableados a múltiples GPIOs (ej. 4-13, 15-18, 38-40).
Tras la revisión del diagrama de hardware oficial de Freenove S3-CAM, este perfil garantiza usar **únicamente pines 100% libres de conflictos** con cámara, PSRAM, USB y SD.

* **ToF SDA / SCL:** 1 / 47 *(Pines libres. Se esquiva GPIO 2 para evitar interferencias con el LED_ON)*
* **Encoder SDA / SCL:** 14 / 21 *(Pines libres y no utilizados por otros módulos)*
* **HX711 DT / SCK:** 41 / 42 *(Pines libres sin funciones de strapping)*

> **Peligro OPI PSRAM (35, 36, 37):**
> Las placas con memoria PSRAM Octal (OPI), como la WROOM-1 N16R8, utilizan físicamente los pines **35, 36 y 37** para comunicarse con este chip de expansión de RAM. Cualquier intento de controlarlos por software (mediante `pinMode`, `digitalRead` o `digitalWrite`) interrumpe este bus crítico, corrompiendo la memoria del sistema y provocando un congelamiento inmediato del microcontrolador o un reinicio por pánico de Watchdog (`TG1WDT_SYS_RST`). Por ello, se prohíbe estrictamente interactuar con ellos.
>
> **Conflicto de GPIO 2 (LED_ON):**
> El pin **GPIO 2** está conectado físicamente a un LED indicador en la placa. Usar GPIO 2 para I2C (ej. ToF SCL) agrega una carga resistiva y capacitiva (el LED + su resistencia a tierra), lo que deforma los flancos de subida necesarios para el bus I2C (basados en resistencias pull-up). Esto resulta en fallos de comunicación aleatorios y parpadeo errático. Por seguridad, reubicamos el canal SCL al **GPIO 47**, que está 100% libre.

### C. Perfil Custom (Personalizado)

Permite al Docente o Líder de Mesa cruzar cualquier combinación. La UI validará que:

1. No se asigne el mismo pin a dos sensores diferentes (Validación JS de array único).
2. Lanzará una advertencia si se eligen pines que están en la lista negra local del tipo de cámara conectada (ej. si se intenta usar el GPIO 4, que es datos de cámara en la Freenove).
