# 🧪 Physys Lab v6.1 — Laboratorio de Física Portátil

> **Plataforma ESP32-S3 WROOM-1** | WiFi AP + Captive Portal | PWA Offline-First

---

## 📋 Especificaciones de Hardware

| Componente | Detalle |
|------------|---------|
| **MCU** | ESP32-S3 QFN56 rev v0.2 |
| **Flash** | 16 MB (QIO) |
| **PSRAM** | 8 MB embebida (AP_3v3, QSPI) |
| **Sensores** | VL53L0X (ToF I2C), AS5600 (Encoder I2C), HX711 (Celda de carga) |
| **LED** | WS2812 en GPIO48 (indicador de estado) |
| **WiFi** | AP "Physys-Lab" con portal cautivo |
| **IP** | 192.168.4.1 |

## 🔌 Pinout

| GPIO | Función | Bus |
|------|---------|-----|
| 4 | SDA (VL53L0X + AS5600) | I2C |
| 5 | SCL (VL53L0X + AS5600) | I2C |
| 6 | HX711 DT (datos) | Digital |
| 7 | HX711 SCK (clock) | Digital |
| 48 | WS2812 LED | RMT |
| 0 | BOOT / Factory Reset | Input |

## 🚀 Setup Rápido

### Prerrequisitos
- [PlatformIO](https://platformio.org/) (VS Code extension o CLI)
- Cable USB-C → ESP32-S3

### Compilar y flashear
```bash
# Compilar firmware
pio run

# Subir firmware al ESP32
pio run --target upload

# Subir webapp (LittleFS) al ESP32
pio run --target uploadfs
```

### Conectarse
1. Desde el celular, busca la red WiFi **"Physys-Lab"**.
2. Conéctate (sin contraseña).
3. El portal cautivo abre automáticamente → `http://192.168.4.1`

## 📊 Variables de Medición

### Cinemática Lineal (VL53L0X)
| Variable | Clave JSON | Unidad |
|----------|-----------|--------|
| Posición | `dist` | mm |
| Rapidez | `vel` | m/s |
| Aceleración | `acc` | m/s² |

### Cinemática Angular (AS5600)
| Variable | Clave JSON | Unidad |
|----------|-----------|--------|
| Ángulo | `angleDeg` / `angleRad` | ° / rad |
| Velocidad angular | `angVel` | rad/s |
| Aceleración angular | `angAcc` | rad/s² |

### Dinámica (HX711)
| Variable | Clave JSON | Unidad |
|----------|-----------|--------|
| Peso bruto | `weight` | g |
| Masa | `mass` | kg |
| Peso vertical | `weightN` | N |

## 🔗 API REST

| Endpoint | Método | Descripción |
|----------|--------|-------------|
| `/` | GET | WebApp (PWA) |
| `/ws` | WebSocket | Streaming 20Hz (JSON) |
| `/api/status` | GET | Estado del sistema (heap, PSRAM, uptime) |
| `/api/gpio` | GET | Estado de 35 pines GPIO |
| `/api/data` | GET | Exportar experimentos guardados |
| `/api/data` | POST | Guardar experimento (JSON) |
| `/api/python` | GET | Leer script Python actual |
| `/api/python` | POST | Guardar script Python |
| `/api/storage` | GET | Uso de particiones LittleFS |
| `/api/export` | GET | Exportar todos los datos |

## 💾 Mapa de Memoria (16MB Flash)

```
┌──────────────────────┐ 0x000000
│ Bootloader (20 KB)   │
├──────────────────────┤ 0x009000
│ NVS (16 KB)          │
├──────────────────────┤ 0x00D000
│ OTA Data (8 KB)      │
├──────────────────────┤ 0x010000
│ App (6 MB)           │  ← Firmware C++ (usa 18.5%)
├──────────────────────┤ 0x610000
│ Storage (6 MB)       │  ← WebApp + sensor_logic.py (LittleFS)
├──────────────────────┤ 0xC10000
│ UserData (~4 MB)     │  ← Experimentos JSON (LittleFS)
└──────────────────────┘ 0xFFFFFF
```

## 🔴 LED de Estado (WS2812)

| Color | Significado |
|-------|-------------|
| 🟡 Amarillo | Inicializando |
| ⚪ Blanco | Listo (WiFi AP activo, sin medición) |
| 🔵 Azul | Midiendo (WebSocket streaming) |
| 🟢 Verde | Dato guardado exitosamente |
| 🟣 Magenta | Error (script fallido, sensor desconectado) |

## 🔄 Factory Reset

**Mantén presionado el botón BOOT (GPIO0) por 10 segundos.**

Esto borra:
- Datos de experimentos
- Configuración del dispositivo
- WiFi credentials

El LED parpadea rápido durante el reset.

## 🎓 Branding

Edita `config.json` en la partición de sistema para personalizar:
```json
{
    "institution": "Universidad Militar Nueva Granada",
    "faculty": "Facultad de Ingeniería",
    "lab_name": "Physys Lab v6.1",
    "logo_url": "/assets/logo.png"
}
```

## 📁 Estructura del Proyecto

```
ESP32S3 PHYSYS LAB/
├── platformio.ini          # Configuración PlatformIO
├── partitions.csv          # Mapa de particiones 16MB
├── src/
│   └── main.cpp            # Firmware C++ (~500 líneas)
├── data/
│   ├── sensor_logic.py     # Script Python del estudiante
│   └── www/
│       ├── index.html      # WebApp principal
│       ├── styles.css      # Estética Gemini v6
│       ├── app.js          # WebSocket + Charts + Editor
│       ├── manifest.json   # PWA manifest
│       └── sw.js           # Service Worker (offline)
└── README.md               # Este archivo
```

## 📄 Licencia

Proyecto académico — UMNG 2026.
