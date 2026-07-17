#include <Arduino.h>
#include <Wire.h>
#include "VI5300_UserPlatform.h"
#include "VI5300_API.h"

extern TwoWire* tofBus;

VI5300_Status I2C_WriteXBytes(uint8_t startaddr, uint8_t *buf, uint8_t len) {
    if (!tofBus) return VI5300_ERROR;
    tofBus->beginTransmission(gSalve >> 1); // gSalve is 8-bit (0xD8). Arduino Wire uses 7-bit (0x6C)!
    tofBus->write(startaddr);
    for (uint8_t i = 0; i < len; i++) {
        tofBus->write(buf[i]);
    }
    if (tofBus->endTransmission() == 0) {
        return VI5300_OK;
    }
    return VI5300_ERROR;
}

VI5300_Status I2C_ReadXBytes(uint8_t startaddr, uint8_t *buf, uint8_t len) {
    if (!tofBus) return VI5300_ERROR;
    tofBus->beginTransmission(gSalve >> 1);
    tofBus->write(startaddr);
    if (tofBus->endTransmission(false) != 0) {
        return VI5300_ERROR;
    }
    uint8_t read_len = tofBus->requestFrom((uint8_t)(gSalve >> 1), len);
    if (read_len == len) {
        for (uint8_t i = 0; i < len; i++) {
            buf[i] = tofBus->read();
        }
        return VI5300_OK;
    }
    return VI5300_ERROR;
}

VI5300_Status WriteOneReg(uint8_t addr, uint8_t value) {
    return I2C_WriteXBytes(addr, &value, 1);
}

VI5300_Status ReadOneReg(uint8_t addr, uint8_t *value) {
    return I2C_ReadXBytes(addr, value, 1);
}

VI5300_Status WriteCommand(uint8_t cmd) {
    return WriteOneReg(VI5300_REG_CMD, cmd);
}

VI5300_Status I2C_2V1_WriteOneReg(uint8_t addr, uint8_t value) {
    return WriteOneReg(addr, value);
}

VI5300_Status I2C_2V1_ReadOneReg(uint8_t addr, uint8_t *value) {
    return ReadOneReg(addr, value);
}
