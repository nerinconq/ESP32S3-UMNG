#ifndef USB_HOST_MSC_WRAPPER_H
#define USB_HOST_MSC_WRAPPER_H

#include <Arduino.h>
#include "usb_host.hpp"
#include "usb_msc.hpp"

class UsbHostMSC {
public:
    UsbHostMSC() : host(nullptr), msc(nullptr) {}

    bool begin() {
        host = new USBhost();
        if (!host->init()) return false;
        
        // El host se encargará de detectar el dispositivo. 
        // Para simplificar, intentaremos obtener la instancia de MSC cuando se conecte.
        return true;
    }

    void loop() {
        // Esta librería requiere manejo de eventos o polling si no se usan tareas
    }

    bool appendLog(const char* filename, const char* line) {
        char fullPath[64];
        snprintf(fullPath, sizeof(fullPath), "/usb/%s", filename);
        FILE* f = fopen(fullPath, "a");
        if (f) {
            fprintf(f, "%s\n", line);
            fclose(f);
            return true;
        }
        return false;
    }

    bool isConnected() {
        return (USBmscDevice::getInstance() != nullptr);
    }

    void onConnected(void (*cb)()) { _onConnected = cb; }
    void onDisconnected(void (*cb)()) { _onDisconnected = cb; }

private:
    USBhost* host;
    USBmscDevice* msc;
    void (*_onConnected)() = nullptr;
    void (*_onDisconnected)() = nullptr;
};

#endif
