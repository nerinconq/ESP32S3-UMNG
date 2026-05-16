#ifndef USB_HOST_MSC_WRAPPER_H
#define USB_HOST_MSC_WRAPPER_H

#include <Arduino.h>
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "esp_vfs_fat.h"

class UsbHostMSC {
public:
    UsbHostMSC() : _connected(false), _msc_device(nullptr), _vfs_handle(nullptr) {}

    bool begin() {
        const msc_host_driver_config_t msc_config = {
            .create_backround_task = true,
            .task_priority = 5,
            .stack_size = 4096,
            .core_id = 0,
            .callback = msc_event_cb,
            .callback_arg = this,
        };

        esp_err_t err = msc_host_install(&msc_config);
        if (err != ESP_OK) {
            Serial.printf("[USB] Error instalando MSC: %s\n", esp_err_to_name(err));
            return false;
        }

        Serial.println("[USB] Stack MSC (firechip/espressif) inicializado.");
        return true;
    }

    void loop() {}

    bool isConnected() { return _connected; }

    bool appendLog(const char* filename, const char* line) {
        if (!_connected) return false;
        char fullPath[128];
        snprintf(fullPath, sizeof(fullPath), "/usb/%s", filename);
        
        FILE* f = fopen(fullPath, "a");
        if (f) {
            fprintf(f, "%s\n", line);
            fclose(f);
            return true;
        }
        return false;
    }

private:
    bool _connected;
    msc_host_device_handle_t _msc_device;
    msc_host_vfs_handle_t _vfs_handle;

    static void msc_event_cb(const msc_host_event_t *event, void *arg) {
        UsbHostMSC* obj = (UsbHostMSC*)arg;
        
        // Usamos los valores numéricos del enum para evitar problemas de scoping en C++
        // 0 = MSC_DEVICE_CONNECTED, 1 = MSC_DEVICE_DISCONNECTED
        if (event->event == 0) { 
            uint8_t addr = event->device.address;
            Serial.printf("[USB] Dispositivo detectado en addr %d\n", addr);
            obj->handle_connection(addr);
        } else if (event->event == 1) {
            Serial.println("[USB] Dispositivo desconectado");
            obj->handle_disconnection();
        }
    }

    void handle_connection(uint8_t addr) {
        esp_err_t err = msc_host_install_device(addr, &_msc_device);
        if (err != ESP_OK) {
            Serial.printf("[USB] Error instalando dispositivo: %s\n", esp_err_to_name(err));
            return;
        }

        const esp_vfs_fat_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 1024
        };

        err = msc_host_vfs_register(_msc_device, "/usb", &mount_config, &_vfs_handle);
        if (err == ESP_OK) {
            _connected = true;
            Serial.println("[USB] Pendrive montado en /usb");
        } else {
            Serial.printf("[USB] Error VFS: %s\n", esp_err_to_name(err));
        }
    }

    void handle_disconnection() {
        if (_connected) {
            msc_host_vfs_unregister(_vfs_handle);
            msc_host_uninstall_device(_msc_device);
            _connected = false;
        }
    }
};

#endif
