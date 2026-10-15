# 🛠️ Kit Autónomo de Grabación USB-C — Physys Lab ESP32-S3
**Evento Open Day — Universidad Militar Nueva Granada**  
*Versión de Firmware: V_1_16_07_26 (Tag de Git: v1.17-open-day)*

Este kit está diseñado para llevarse en una memoria USB (USB-A o USB-C) y permitir **flashear, rescatar o reasignar mesas en cualquier computador con Windows** (Windows 7, 8, 10 u 11), **sin necesidad de tener instalado Python, PlatformIO ni ninguna otra herramienta.**

---

## 📦 Contenido de este Paquete
```text
Kit_Flasher_USB_OpenDay/
├── bin/
│   ├── base/               <-- Binarios para tarjetas Base y Clones (QSPI / Sin PSRAM)
│   │   ├── bootloader.bin  (0x0000)
│   │   ├── partitions.bin  (0x8000)
│   │   ├── boot_app0.bin   (0xd000)
│   │   ├── firmware.bin    (0x10000 - Firmware compilado V_1_16_07_26)
│   │   └── littlefs.bin    (0x610000 - Web LittleFS comprimida)
│   └── cam/                <-- Binarios para tarjetas CAM (Octal OPI)
│       └── ...
├── tools/
│   └── esptool.exe         <-- Herramienta oficial Espressif 100% standalone (NO requiere Python)
├── QR_Mesas/               <-- Copia de los codigos QR y archivo registro_mesas.csv
├── grabar_desde_usb.bat    <-- Script automatizado con 1 clic
└── SHA256_HASHES.txt       <-- Verificacion de integridad de cada archivo binario
```

---

## 🚀 Instrucciones de Uso Rápido (En Cualquier Computador)

1. Conecta la memoria USB a tu computador.
2. Conecta la tarjeta ESP32-S3 a un puerto USB mediante su cable de datos.
3. Haz doble clic en el archivo **`grabar_desde_usb.bat`**.
4. El programa detectará automáticamente el puerto COM (ejemplo: `COM13`, `COM5`). Presiona `Enter` para seleccionarlo.
5. Selecciona el tipo de tarjeta:
   * **1) Base / Clon:** Para tarjetas estándar o clones chinos.
   * **2) CAM:** Para módulos con cámara integrada (PSRAM Octal OPI).
6. Selecciona el modo de operación:
   * **Opción 1 (Grabación Completa):** Graba todo el sistema en aproximadamente 30 segundos. Conserva los datos guardados en NVS.
   * **Opción 2 (Modo Rescate):** Si la tarjeta no responde o tiene memoria corrupta, primero borra toda la memoria flash (`erase_flash`) y luego graba todo desde cero.
   * **Opción 3 (Solo Asignar Mesa):** No flashea nada; se comunica por puerto serie con la tarjeta ya programada para asignarle su número de mesa.
7. Al finalizar la grabación, el asistente te preguntará el **número de mesa (1-99)**. Escribe el número correspondiente (ej: `5`) y presiona `Enter`.
   * El ESP32 configurará su clave WiFi WPA2 (`umngm-05`) y su canal no interferente correspondiente (1, 6 u 11).

---

## 💡 Solución de Problemas Comunes en Campo

* **El script no detecta el puerto COM:**
  * Si la tarjeta es un clon con chip CH340 o CP210x, asegúrate de que el computador tenga el driver del chip USB-Serial instalado.
  * Si la tarjeta tiene dos puertos USB marcados como `USB` y `UART`, conecta el cable al puerto `USB` (puerto nativo CDC/OTG).
* **Fallo al iniciar la grabación (Timed out waiting for packet header):**
  * Desconecta el cable USB.
  * Mantén presionado con tu dedo el botón **BOOT** de la tarjeta.
  * Conecta el cable USB mientras mantienes presionado BOOT.
  * Suelta el botón BOOT después de 2 segundos.
  * Ejecuta de nuevo `grabar_desde_usb.bat`.
