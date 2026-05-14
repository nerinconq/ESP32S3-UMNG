# 🧪 Physys Lab v9.0 — Laboratorio de Física Portátil (Antigravity Edition)

> **Plataforma ESP32-S3 WROOM-1** | WiFi AP + mDNS | USB Host MSC | PWA Offline-First

---

## 📋 Especificaciones de Hardware

| Componente | Detalle |
|------------|---------|
| **MCU** | ESP32-S3 QFN56 rev v0.2 |
| **Flash** | 16 MB (QIO) |
| **PSRAM** | 8 MB embebida (QSPI) |
| **USB Host** | Soporte Nativo para Pendrives (FAT32/exFAT) |
| **Sensores** | ToF (VL53L0X, L1X, L5CX), AS5600 (Encoder), HX711 (Celda de carga) |
| **LED** | WS2812 en GPIO48 (Semáforo de estado) |
| **WiFi** | AP "Physys-Lab-[MAC]" con mDNS |
| **IP / Host** | `192.168.4.1` o `http://physyslab.local` |

## 🔌 Pinout Actualizado

| GPIO | Función | Bus |
|------|---------|-----|
| 4 | SDA (ToF Bus 0) | I2C |
| 5 | SCL (ToF Bus 0) | I2C |
| 10 | SDA (Encoder Bus 1) | I2C |
| 11 | SCL (Encoder Bus 1) | I2C |
| 6 | HX711 DT | Digital |
| 7 | HX711 SCK | Digital |
| 48 | WS2812 LED | RMT |
| 0 | BOOT / Factory Reset | Input |

## 🚀 Conectividad Pro

1. **WiFi**: Busca la red **"Physys-Lab-XXXX"** (donde XXXX es parte de la MAC).
2. **Acceso**: Abre tu navegador en `http://physyslab.local` (requiere soporte mDNS en el dispositivo cliente).
3. **CORS**: El sistema permite peticiones externas para integración con Python/Matlab.

## 💾 Modo Híbrido USB (H6)

El sistema ahora puede exportar datos directamente a un pendrive USB-C:
1. En el Dashboard, pulsa **"Exportar a USB"**.
2. El dispositivo se reiniciará en **Modo Exportación** (LED Azul intermitente).
3. Conecta el pendrive. El sistema copiará automáticamente todos los experimentos de la LittleFS al USB.
4. El LED parpadeará en **Verde** cuando la copia sea segura. Retira el pendrive y el sistema volverá al modo normal.

## 🔴 Semáforo LED (Estados)

| Color | Significado |
|-------|-------------|
| 🟡 Amarillo | Inicializando / Formateando |
| ⚪ Blanco | Listo (En espera) |
| 🟠 Naranja | **Modo Gatillo**: Esperando detección de movimiento |
| 🔵 Azul | **Midiendo**: Streaming activo o Grabando en PSRAM |
| 🟢 Verde | Experimento guardado o Exportación USB exitosa |
| 🟣 Magenta | Error crítico (Sensor desconectado o fallo de montaje USB) |

## 📁 Estructura del Proyecto

```
ESP32S3 PHYSYS LAB/
├── src/                    # Firmware C++ Core (v9.0)
├── data/www/               # Frontend PWA Premium
├── Planes_AI/              # Documentación técnica y Roadmap
├── graphify-out/           # Grafo de arquitectura (AI Context)
└── platformio.ini          # Configuración y dependencias
```

## 📄 Licencia e Institución

Proyecto desarrollado para la **Universidad Militar Nueva Granada** (UMNG).
Investigador Principal: **Nelson Rincón**.
Asistente AI: **Antigravity**.

---
© 2026 Physys Lab Team.
