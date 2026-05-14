# Mapa del Proyecto Physys Lab - GraphyFYY 🚀

Este diagrama representa la arquitectura integral del sistema, incluyendo la reciente implementación del modo híbrido USB y la sincronización con la nube.

```mermaid
graph TD
    %% Centro de Control
    ESP32[ESP32-S3 Core]
    
    %% Firmware / Capas de Software
    subgraph Firmware_Modules ["Firmware Modules (Arduino + ESP-IDF)"]
        USB[USB Host MSC Native - v1.1.4]
        WIFI[WiFi AP Mode - Portal Cautivo]
        SENSORS[Drivers Sensores: HX711, AS5600, ToF]
        FS[Filesystems: SystemFS & DataFS - LittleFS]
        WS[WebSocket Server - Puerto 80]
    end
    
    %% Almacenamiento y Datos
    subgraph Storage ["Almacenamiento"]
        FLASH[Internal Flash Memory]
        PENDRIVE[USB Flash Drive - FAT32/exFAT]
        NVS[Preferences - Boot Mode Flag]
    end
    
    %% Lógica de Negocio
    subgraph Logic ["Lógica de Control v9.1"]
        NORMAL[Modo Normal: Medición Libre]
        AUTO[Toma Auto: Parada Inteligente]
        FILTER[Filtro Anti-8190: Signal Clipping]
        EXPORT[Modo Exportación: LittleFS -> USB]
    end
    
    %% Interfaz de Usuario (Web App)
    subgraph WebApp ["Web Interface (PWA)"]
        UI[Dashboard Premium - JS/CSS]
        IDB[IndexedDB - Cache Local]
        SYNC[Firebase Sync - Cloud Persistence]
    end

    %% Relaciones
    ESP32 --> Firmware_Modules
    ESP32 --> Storage
    
    %% Ciclo de Datos
    SENSORS --> FILTER
    FILTER --> NORMAL
    FILTER --> AUTO
    AUTO -- "Stop a distMin-10" --> NORMAL
    NORMAL --> WS
    WS --> UI
    
    %% Ciclo USB
    UI -- "Comando USB_EXPORT" --> NVS
    NVS -- "Reboot" --> EXPORT
    EXPORT -- "Copia de Seguridad" --> PENDRIVE
    EXPORT -- "Auto-Reset" --> NORMAL
    
    %% Sincronización
    UI --> IDB
    IDB -- "Sync Offline/Online" --> SYNC
    
    %% Estilos
    style ESP32 fill:#6366f1,stroke:#fff,stroke-width:2px,color:#fff
    style EXPORT fill:#0ea5e9,stroke:#fff,stroke-width:2px,color:#fff
    style FILTER fill:#10b981,stroke:#fff,stroke-width:2px,color:#fff
    style SYNC fill:#f59e0b,stroke:#fff,stroke-width:2px,color:#fff
    style FIRMWARE fill:#f8fafc,stroke:#334155
```

## Directorio de Archivos Críticos

| Carpeta / Archivo | Función |
| :--- | :--- |
| `src/main.cpp` | Lógica principal, Filtro Anti-8190 y Paradas Inteligentes. |
| `src/idf_component.yml` | Dependencia nativa `espressif/usb_host_msc`. |
| `data/www/` | Frontend de la aplicación (Dashboard + Lógica JS). |
| `Planes_AI/` | Documentación técnica persistente (Checklists, Mapas). |

## Estado de Implementación
- [x] **H1-H4**: Sensores y UI Base.
- [x] **H5**: Preparación Sincronización Firebase.
- [x] **H6**: Modo Híbrido USB Host MSC.
- [x] **H8 (NUEVO)**: Estabilización v9.1 (Filtro de señal y Parada 10mm).
- [ ] **H9**: Escritura masiva a USB Pendrive.
