# 🏗️ Physys Lab Architecture — Graphify Report

This document provides a visual and technical overview of the **Physys Lab v7.0** system, an offline-first physics measurement laboratory built on the ESP32-S3.

---

## 1. 📂 Repository Structure
A logical map of the project files and their roles in the ecosystem.

```mermaid
graph TD
    Root["/ (Project Root)"]
    Root --> PIO["platformio.ini (Build Config)"]
    Root --> Part["partitions.csv (Flash Map)"]
    
    Root --> Src["/src (Firmware)"]
    Src --> Main["main.cpp (C++ Core)"]
    Src --> USB["UsbHostMSC.h (Storage Wrapper)"]
    
    Root --> Data["/data (Filesystem)"]
    Data --> WWW["/www (WebApp)"]
    WWW --> HTML["index.html (PWA Shell)"]
    WWW --> CSS["styles.css (Glassmorphism)"]
    WWW --> JS["app.js (Logic & Charts)"]
    WWW --> FB["firebase-sync.js (Cloud Sync)"]
    WWW --> SW["sw.js (Service Worker)"]
    
    Data --> Logic["sensor_logic.py (User Scripts)"]
    
    Root --> Agents[".agents (AI Skills)"]
    Root --> Learning["arbol de aprendizaje (Educational)"]
```

---

## 2. 🔌 Hardware Architecture
How the ESP32-S3 WROOM-1 interacts with its sensors and indicators.

```mermaid
graph LR
    subgraph "ESP32-S3 WROOM-1"
        MCU["MCU (Tensilica LX7)"]
        Flash["16MB Flash"]
        PSRAM["8MB PSRAM"]
    end

    subgraph "I2C Bus 0 (GPIO 4/5)"
        ToF["VL53L0X / VL53L1X (ToF)"]
    end

    subgraph "I2C Bus 1 (GPIO 10/11)"
        Encoder["AS5600 (Rotary)"]
    end

    subgraph "Digital Pins (GPIO 6/7)"
        HX711["HX711 (Load Cell)"]
    end

    subgraph "RMT Control (GPIO 48)"
        LED["WS2812 RGB LED"]
    end

    subgraph "USB-C Interface"
        USB_C["USB Host MSC"]
    end

    MCU --- Flash
    MCU --- PSRAM
    MCU --- ToF
    MCU --- Encoder
    MCU --- HX711
    MCU --- LED
    MCU --- USB_C
```

---

## 3. 🌐 Software Layers & Communication
The multi-layer stack from low-level drivers to cloud synchronization.

```mermaid
graph TD
    subgraph "Cloud (Google Cloud)"
        Firestore["Firestore Database"]
    end

    subgraph "Client (Mobile/PC Browser)"
        PWA["PWA (Offline Cache)"]
        Charts["uPlot / Chart.js"]
        FB_JS["Firebase SDK (Sync)"]
        IndexedDB["Local IndexedDB"]
    end

    subgraph "ESP32-S3 Firmware (C++)"
        WebServer["AsyncWebServer (Port 80)"]
        WS["WebSocket (/ws @ 20-30Hz)"]
        API["REST API (/api)"]
        LittleFS["LittleFS (Storage/UserData)"]
        Sensors["Sensor Drivers (I2C/Digital)"]
    end

    Sensors --> WS
    WS <--> PWA
    PWA --> Charts
    PWA <--> IndexedDB
    IndexedDB <--> FB_JS
    FB_JS <--> Firestore
    API <--> WebServer
```

---

## 💾 Memory Partitioning (16MB Flash)
Visual breakdown of the `partitions.csv` configuration.

```mermaid
pie title Flash Storage Allocation
    "C++ Firmware (App)" : 6
    "WebApp & System (LittleFS)" : 6
    "User Experiments (LittleFS)" : 4
```

---

## 🔄 Measurement Lifecycle
The sequence of events during a single experiment.

```mermaid
sequenceDiagram
    participant User as Student (Browser)
    participant ESP as ESP32-S3 Core
    participant Sensor as Physical Sensors
    participant FS as LittleFS / USB

    User->>ESP: WebSocket "START"
    ESP->>Sensor: Initialize / Zero
    loop Continuous Measurement
        Sensor->>ESP: Raw Data (I2C/Digital)
        ESP->>ESP: Kinematic Calc (Vel/Acc)
        ESP->>User: JSON Stream (WebSocket)
        User->>User: Real-time Plotting
    end
    User->>ESP: WebSocket "STOP"
    ESP->>FS: Save Experiment (JSON)
    ESP->>User: Confirm Saved
```

---

## 🚦 System Status Codes (LED)
| Color | Meaning | Action |
|-------|---------|--------|
| ⚪ White | Ready / Idle | System is waiting for connection |
| 🔵 Blue | Measuring | Real-time streaming active |
| 🟢 Green | Success | Experiment saved or synced |
| 🟡 Yellow | Warning | Memory nearly full |
| 🟣 Magenta | Script Error | Python logic failed (H3) |
| 🔴 Red | Critical | Battery low or hardware failure |
