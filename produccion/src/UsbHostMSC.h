#ifndef USB_HOST_MSC_WRAPPER_H
#define USB_HOST_MSC_WRAPPER_H

#include <Arduino.h>

// STUB para compilar sin usb-host-msc en env CAM
class UsbHostMSC {
public:
    UsbHostMSC() : _connected(false) {}
    bool begin() {
        Serial.println("[USB] USB Host MSC deshabilitado (modo CDC activo)");
        return false;
    }
    void loop() {}
    bool isConnected() { return false; }
    bool appendLog(const char* f, const char* l) { (void)f; (void)l; return false; }
private:
    bool _connected;
};

#endif
