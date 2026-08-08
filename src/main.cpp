/*
 * Physys Measurement System — Núcleo Base (H1)
 * ESP32-S3 WROOM-1 (N16R8) | QIO Flash + QSPI PSRAM
 * 
 * Arquitectura: Firmware C++/Arduino con control total del hardware.
 * Sensores: VL53L0X (ToF), AS5600 (Encoder), HX711 (Celda de Carga).
 * Red: WiFi AP "Physys-Lab" + Portal Cautivo + WebSocket streaming.
 * LED: WS2812 en GPIO48 (Freenove Pinout) como semáforo de estado.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <VL53L1X.h>
#include <VL6180X.h>
#include <SparkFun_VL53L5CX_Library.h>
#include <AS5600.h>
#include <HX711.h>
#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>
#include <FastLED.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "UsbHostMSC.h" // Wrapper de la librería chegewara
#include <VI5300_API.h>

Preferences preferences;
UsbHostMSC usbHost;
bool usbConnected = false;
bool usbLogActive = false;
// Flag para control de reinicio en modo USB
String bootMode = "normal";

// ═══════════════════════════════════════════════════════════════
// PINOUT (Freenove ESP32-S3 WROOM Reference)
// ═══════════════════════════════════════════════════════════════
// Bus I2C #0 — ToF VL53L0X (dirección 0x29)
int pinTofSda = 4;
int pinTofScl = 5;
// Bus I2C #1 — AS5600 Encoder (dirección 0x36)
int pinEncSda = 10;
int pinEncScl = 11;
int pinHxDt = 6;
int pinHxSck = 7;
#define LED_PIN        48   // GPIO48 — WS2812 onboard (Freenove / YD-ESP32S3)
#define NUM_LEDS       1
int pinLedRgb = LED_PIN;    // GPIO variable para LED dinámico (NVS persistente)
#define FACTORY_RESET_PIN 0 // GPIO0 — BOOT button = Factory Reset (10s hold)

// ═══════════════════════════════════════════════════════════════
// OBJETOS GLOBALES
// ═══════════════════════════════════════════════════════════════
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;

// Control Multiusuario
String currentLeaderIp = "";
String currentTeacherIp = "";

TwoWire I2C_TOF = TwoWire(0);  // Bus 0 para ToF
TwoWire I2C_ENC = TwoWire(1);  // Bus 1 para Encoder

// Objetos de sensores (se inicializará el que corresponda)
VL53L0X  tof0X;
VL53L1X  tof1X;
VL6180X  tof6180;
SparkFun_VL53L5CX tof5CX;

enum ToFModel { MODEL_L0X, MODEL_L1X, MODEL_L1X_V2, MODEL_6180, MODEL_L5CX, MODEL_TOFSENSE, MODEL_TFMINI_S, MODEL_TOF10120, MODEL_VI5300, MODEL_NONE };
ToFModel activeToF = MODEL_L0X;
String currentToFModel = "vl53l0x";
String currentEncoderModel = "as5600";
String currentWeightMode = "hx711";
int sampleRateMs = 10; // Frecuencia por defecto: 100 Hz (10 ms)

// UART para sensores de distancia (TOFSense, TFmini-S) en pines HX711 cuando no hay HX711
HardwareSerial uartSensor(1);
bool uartSensorReady = false;

AS5600 encoder(&I2C_ENC);  // AS5600 default en Bus 1
AS5600 encoder_alt(&I2C_TOF); // AS5600 alternativo en Bus 0
AS5600* activeEncoder = &encoder; // Puntero al encoder activo
TwoWire* tofBus = &I2C_TOF; // Puntero al bus donde está el ToF
HX711 loadCell;
NAU7802 nauScale;
bool useNAU7802 = false;
float massCalibrationFactor = 420.0f; // Constante de calibración de masa (def: 420)
CRGB leds[NUM_LEDS];

// Particiones LittleFS
// 'storage' = WebApp + Sistema (6MB) — PlatformIO uploadfs target
// 'userdata' = Experimentos + Config estudiantes (4MB)
fs::LittleFSFS SystemFS;
fs::LittleFSFS DataFS;

// Estado del sistema
// Control de medición por sensor
struct SensorMeasure {
  bool active = false;
  unsigned long startTime = 0;
};

// Estructura para almacenamiento en PSRAM (Captura de alta velocidad)
struct DataPoint {
  uint32_t t;
  float dist;
  float vel;
  float weight;
  float angle;
};

#define MAX_SAMPLES 60000 
DataPoint* highSpeedBuffer = nullptr;
uint32_t bufferIndex = 0;

struct SystemState {
  bool tofReady = false;
  bool encoderReady = false;
  bool loadCellReady = false;
  bool measuring = false;       // compatibilidad global
  SensorMeasure tofMeasure;
  SensorMeasure encMeasure;
  SensorMeasure hxMeasure;
  // Cinemática Lineal (VL53L0X)
  float lastDistance = 0;       // mm
  float lastVelocity = 0;       // m/s
  float lastAccel = 0;          // m/s²
  float prevDistance = 0;
  float prevVelocity = 0;
  // Cinemática Angular (AS5600)
  float lastAngleDeg = 0;       // grados (acumulativo)
  float lastAngleRad = 0;       // radianes (acumulativo)
  float angularVelRad = 0;      // rad/s
  float angularAccelRad = 0;    // rad/s²
  float prevAngleRad = 0;
  float prevAngularVel = 0;
  int lastRawAngle = -1;        // Para detectar vueltas (0-4095)
  float cumulativeAngleDeg = 0; // Acumulador multi-vuelta
  bool invertEncoder = false;   // Invertir sentido de giro
  // Dinámica (HX711)
  float lastWeight = 0;         // gramos
  float filteredWeight = 0;     // Para el filtro digital
  bool useHxFilter = true;      // Activar/desactivar filtro
  bool hxHighStability = false; // Modo 16-bit para reducir ruido
  // Modo Gatillo (Sample Trigger)
  bool triggerEnabled = false;
  bool isWaitingForTrigger = false;
  float initialDistance = 0;
  float tubeLength = 0;         // mm (0 = desactivado, sin auto-stop)
  float triggerThreshold = 2.0;  // mm (Zona muerta)
  float distMin = 30.0;
  float distMax = 2000.0;
  String tofRange = "short";    // Perfil VL53L0X: "short" o "long"
  
  uint32_t samplesCount = 0;
  bool bufferFull = false;
  unsigned long lastSampleTime = 0;
  unsigned long lastTofTime = 0;
  unsigned long measurementStartTime = 0;
  int consecutiveGlitches = 0;
} state;

// Delay para asegurar guardado en NVS antes de reboot
void safeReboot() {
  Serial.println("[SYS] Preparando reinicio... Guardando buffers...");
  delay(1000); // 1 segundo de seguridad
  ESP.restart();
}

void resetBuffer() {
  bufferIndex = 0;
  state.samplesCount = 0;
  state.bufferFull = false;
}

// Forward declarations
void checkGlobalStop();

// ═══════════════════════════════════════════════════════════════
// SEMÁFORO LED (RMT via FastLED)
// ═══════════════════════════════════════════════════════════════
void initFastLED(int pin) {
  switch (pin) {
    case 48: FastLED.addLeds<WS2812, 48, GRB>(leds, NUM_LEDS); break;
    case 38: FastLED.addLeds<WS2812, 38, GRB>(leds, NUM_LEDS); break;
    case 21: FastLED.addLeds<WS2812, 21, GRB>(leds, NUM_LEDS); break;
    case 8:  FastLED.addLeds<WS2812, 8,  GRB>(leds, NUM_LEDS); break;
    case 2:  FastLED.addLeds<WS2812, 2,  GRB>(leds, NUM_LEDS); break;
    case 18: FastLED.addLeds<WS2812, 18, GRB>(leds, NUM_LEDS); break;
    case 47: FastLED.addLeds<WS2812, 47, GRB>(leds, NUM_LEDS); break;
    default: FastLED.addLeds<WS2812, 48, GRB>(leds, NUM_LEDS); break;
  }
}

void setLED(CRGB color) {
  leds[0] = color;
  FastLED.show();
}

void blinkLED(CRGB color, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    setLED(color);
    delay(delayMs);
    setLED(CRGB::Black);
    delay(delayMs);
  }
}

// ═══════════════════════════════════════════════════════════════
// DRIVERS DE SENSORES MANUALES (TOF10120 y VI5300)
// ═══════════════════════════════════════════════════════════════
float readToF10120(TwoWire& bus) {
  bus.beginTransmission(0x52);
  bus.write(0x00);
  bus.endTransmission(false);
  bus.requestFrom((uint8_t)0x52, (uint8_t)2);
  if (bus.available() >= 2) {
    uint8_t high = bus.read();
    uint8_t low = bus.read();
    uint16_t dist = (high << 8) | low;
    if (dist > 0 && dist < 2500) return (float)dist;
  }
  return -1;
}

bool validateI2CDevice(TwoWire& bus, uint8_t addr) {
  // 1. Probar conexión básica
  bus.beginTransmission(addr);
  if (bus.endTransmission() != 0) {
    return false;
  }
  
  // 2. Validación específica por dirección para descartar buses flotantes
  if (addr == 0x29) {
    // VL53L0X / VL53L1X / VL53L5CX: leer un registro y verificar que no sea 0x00 ni 0xFF
    bus.beginTransmission(addr);
    bus.write(0xC0); // IDENTIFICATION_MODEL_ID
    if (bus.endTransmission() != 0) return false;
    if (bus.requestFrom(addr, (uint8_t)1) == 1) {
      uint8_t id = bus.read();
      return (id != 0x00 && id != 0xFF);
    }
    return false;
  }
  else if (addr == 0x36) {
    // AS5600: leer STATUS (0x0B), ZMCO (0x0C), RAW ANGLE MSB (0x0D)
    // Descartar bus flotante (todo 0x00 o todo 0xFF)
    bus.beginTransmission(addr);
    bus.write(0x0B);
    if (bus.endTransmission() != 0) return false;
    if (bus.requestFrom(addr, (uint8_t)3) == 3) {
      uint8_t status = bus.read();
      uint8_t zmco = bus.read();
      uint8_t raw_msb = bus.read();
      if ((status == 0x00 && zmco == 0x00 && raw_msb == 0x00) ||
          (status == 0xFF && zmco == 0xFF && raw_msb == 0xFF)) {
        return false;
      }
      return true;
    }
    return false;
  }
  else if (addr == 0x6C) {
    // VI5300 (0x6C): leer chip ID (0x06), debe retornar 0xD8
    bus.beginTransmission(addr);
    bus.write(0x06);
    if (bus.endTransmission() != 0) return false;
    if (bus.requestFrom(addr, (uint8_t)1) == 1) {
      uint8_t id = bus.read();
      return (id == 0xD8);
    }
    return false;
  }
  else if (addr == 0x52) {
    // TOF10120: leer 2 bytes, no deben ser ambos 0x00 o 0xFF
    if (bus.requestFrom(addr, (uint8_t)2) == 2) {
      uint8_t b1 = bus.read();
      uint8_t b2 = bus.read();
      if ((b1 == 0x00 && b2 == 0x00) || (b1 == 0xFF && b2 == 0xFF)) {
        return false;
      }
      return true;
    }
    return false;
  }
  else if (addr == 0x2A) {
    // NAU7802: leer Revision ID (0x1F), debe ser 0x0F
    bus.beginTransmission(addr);
    bus.write(0x1F);
    if (bus.endTransmission() != 0) return false;
    if (bus.requestFrom(addr, (uint8_t)1) == 1) {
      uint8_t rev = bus.read();
      return (rev == 0x0F);
    }
    return false;
  }
  else {
    // Caso por defecto para otras direcciones
    if (bus.requestFrom(addr, (uint8_t)1) == 1) {
      uint8_t b = bus.read();
      return (b != 0x00 && b != 0xFF);
    }
    return false;
  }
}

bool initToF10120(TwoWire& bus) {
  if (validateI2CDevice(bus, 0x52)) {
    Serial.println("[OK] ToF10120 detectado en 0x52");
    return true;
  }
  return false;
}

void configureVI5300Params(uint8_t &fps, uint32_t &intecounts) {
  uint32_t target_fps = 1000 / max(sampleRateMs, 2);
  if (target_fps < 1) target_fps = 1;
  
  if (state.tofRange == "long") {
    // Modo largo: máxima integración para rango completo, cap a 15 FPS
    intecounts = 262144;
    fps = min((uint32_t)15, target_fps);
  } else {
    // Limitar FPS por capacidad de hardware del VI5300
    fps = min((uint32_t)90, target_fps);
    if (fps < 1) fps = 1;
    
    // Calcular tiempo de frame basado en los FPS reales
    uint32_t fps_time_ns = 1000000000 / fps;
    
    // Para evitar subdesbordamiento en el cálculo del delay:
    // intecounts * 146.3 <= fps_time_ns - 2,000,000
    if (fps_time_ns > 2000000) {
      uint32_t max_inte = (fps_time_ns - 2000000) * 10 / 1463;
      if (max_inte > 200000) max_inte = 200000; // Cap para evitar valores excesivos
      if (max_inte < 30000) max_inte = 30000;   // Mínimo de integración razonable
      intecounts = max_inte;
    } else {
      intecounts = 30000;
    }
  }
}

bool initVI5300(TwoWire& bus) {
  tofBus = &bus;
  gSalve = 0xD8; // gSalve se maneja en 8-bit en la API de VisionICs (0x6C << 1)
  
  uint8_t id = VI5300_Device_Check();
  if (id != 0xD8) {
    Serial.printf("[ERR] VI5300 check falló. ID leído = 0x%02X (esperado 0xD8)\n", id);
    return false;
  }
  
  Serial.println("[VI5300] Chip detectado nativamente. Descargando firmware oficial...");
  VI5300_init(); // Carga FW + Set_Integralcounts_Frame(30, 131072) por defecto
  
  // Verificar si el firmware se cargó con éxito leyendo el registro de estado de ejecución
  uint8_t stat = 0;
  ReadOneReg(0x08, &stat);
  if (stat != 0x55 && stat != 0x66) {
    Serial.printf("[ERR] VI5300 firmware run check falló: 0x%02X\n", stat);
    return false;
  }
  
  // Configurar FPS y rango de manera dinámica y segura
  uint8_t fps;
  uint32_t intecounts;
  configureVI5300Params(fps, intecounts);
  VI5300_Set_Integralcounts_Frame(fps, intecounts);
  
  if (VI5300_Start_Continuous_Measure() != VI5300_OK) {
    Serial.println("[ERR] VI5300 error al iniciar medición continua");
    return false;
  }
  
  Serial.printf("[OK] VI5300 inicializado: %d FPS, intecounts=%u, modo %s\n",
                fps, intecounts, state.tofRange == "long" ? "Long" : "Short");
  return true;
}

float readVI5300(TwoWire& bus) {
  tofBus = &bus;
  VI5300_Dist_TypeDef distData;
  VI5300_Status status = VI5300_Get_Measure_Data(&distData);
  if (status == VI5300_OK) {
    // Confianza > 10 y rango plausible (0-5000 mm para el VI5300)
    if (distData.confidence > 10 && distData.millimeter >= 0 && distData.millimeter < 5000) {
      return (float)distData.millimeter;
    }
  }
  return -1; // Dato inválido → se conserva la última lectura válida
}

// ═══════════════════════════════════════════════════════════════
// CONFIGURACIÓN PERSISTENTE
// ═══════════════════════════════════════════════════════════════
void loadSettings() {
  preferences.begin("physys", true);
  currentToFModel = preferences.getString("tof_model", "vl53l0x");
  currentEncoderModel = preferences.getString("enc_model", "as5600");
  currentWeightMode = preferences.getString("weight_mode", "hx711");
  usbLogActive = preferences.getBool("usb_log", false);
  sampleRateMs = preferences.getInt("sample_rate", 10); // Default 100Hz (10ms)
  state.invertEncoder = preferences.getBool("inv_enc", false);
  state.useHxFilter = preferences.getBool("hx_filter", true);
  state.hxHighStability = preferences.getBool("hx_high_stab", false);
  state.tofRange = preferences.getString("tof_range", "short");
  massCalibrationFactor = preferences.getFloat("mass_cal", 420.0f);
  
  pinTofSda = preferences.getInt("pin_tof_sda", 4);
  pinTofScl = preferences.getInt("pin_tof_scl", 5);
  pinEncSda = preferences.getInt("pin_enc_sda", 10);
  pinEncScl = preferences.getInt("pin_enc_scl", 11);
  pinHxDt = preferences.getInt("pin_hx_dt", 6);
  pinHxSck = preferences.getInt("pin_hx_sck", 7);
  pinLedRgb = preferences.getInt("pin_led_rgb", LED_PIN);
  preferences.end();
  
  // Validaciones de seguridad - Forzar 10ms (100Hz) si no es válido o es la primera vez
  if (sampleRateMs < 2 || sampleRateMs > 1000) sampleRateMs = 10;
  if (currentToFModel == "" || currentToFModel.length() < 3) currentToFModel = "vl53l0x";
  if (currentEncoderModel == "") currentEncoderModel = "as5600";
  if (currentWeightMode == "") currentWeightMode = "hx711";

  // Auto-recuperación de Bootloop: Si NVS tiene el preset de cámara que causa crash, revertir.
  bool badPreset1 = (pinTofSda == 1 && pinTofScl == 2 && pinEncSda == 3 && pinEncScl == 14 && pinHxDt == 21 && pinHxSck == 26);
  bool badPreset2 = (pinTofSda == 1 && pinTofScl == 2 && pinEncSda == 8 && pinEncScl == 9 && pinHxDt == 45 && pinHxSck == 46);
  bool badPreset3 = (pinTofSda == 1 && pinTofScl == 42 && pinEncSda == 14 && pinEncScl == 21 && pinHxDt == 47 && pinHxSck == 45); // Strapping
  bool badPreset4 = (pinTofSda == 1 && pinTofScl == 2 && pinEncSda == 14 && pinEncScl == 21 && pinHxDt == 41 && pinHxSck == 42); // LED conflicto
  if (badPreset1 || badPreset2 || badPreset3 || badPreset4) {
    Serial.println("[SYS] ¡PELIGRO! Detectado preset CAM conflictivo. Revirtiendo pines a básicos para evitar Bootloop...");
    pinTofSda = 4; pinTofScl = 5;
    pinEncSda = 10; pinEncScl = 11;
    pinHxDt = 6; pinHxSck = 7;
  }

  Serial.printf("[NVS] Configuración cargada: ToF=%s, Enc=%s, Peso=%s, USB_Log=%d, Rate=%d ms (%d Hz)\n", 
                currentToFModel.c_str(), currentEncoderModel.c_str(), currentWeightMode.c_str(), usbLogActive, sampleRateMs, 1000/sampleRateMs);
  
  if (currentToFModel == "vl53l0x") {
    activeToF = MODEL_L0X;
    state.distMin = 30;
    state.distMax = 2000;
  } else if (currentToFModel == "vl53l1x" || currentToFModel == "vl53l1xv2") {
    activeToF = (currentToFModel == "vl53l1x") ? MODEL_L1X : MODEL_L1X_V2;
    state.distMin = 40;
    state.distMax = 4000;
  } else if (currentToFModel == "vl6180") {
    activeToF = MODEL_6180;
    state.distMin = 10;
    state.distMax = 600;
  } else if (currentToFModel == "vl53l5x") {
    activeToF = MODEL_L5CX;
    state.distMin = 20;
    state.distMax = 4000;
  } else if (currentToFModel == "tofsense") {
    activeToF = MODEL_TOFSENSE;
    state.distMin = 10;
    state.distMax = 25000; // TOFSense hasta 25m
  } else if (currentToFModel == "tfmini_s") {
    activeToF = MODEL_TFMINI_S;
    state.distMin = 10;
    state.distMax = 12000; // TFmini-S hasta 12m
  } else if (currentToFModel == "tof10120") {
    activeToF = MODEL_TOF10120;
    state.distMin = 10;
    state.distMax = 2000;
  } else if (currentToFModel == "vi5300") {
    activeToF = MODEL_VI5300;
    state.distMin = 20;
    state.distMax = 4000;
  } else if (currentToFModel == "none") {
    activeToF = MODEL_NONE;
    state.distMin = 0;
    state.distMax = 0;
  } else {
    activeToF = MODEL_L0X;
    state.distMin = 30;
    state.distMax = 2000;
  }
}

void saveSettings() {
  Serial.printf("[NVS] Guardando: ToF=%s, Enc=%s, Peso=%s, Rate=%d ms, InvEnc=%d, MassCal=%.3f\n", 
                currentToFModel.c_str(), currentEncoderModel.c_str(), currentWeightMode.c_str(), sampleRateMs, state.invertEncoder, massCalibrationFactor);
  if (!preferences.begin("physys", false)) {
    Serial.println("[ERR] No se pudo abrir NVS para escritura");
    return;
  }
  preferences.putString("tof_model", currentToFModel);
  preferences.putString("enc_model", currentEncoderModel);
  preferences.putString("weight_mode", currentWeightMode);
  preferences.putBool("usb_log", usbLogActive);
  preferences.putInt("sample_rate", sampleRateMs);
  preferences.putBool("inv_enc", state.invertEncoder);
  preferences.putBool("hx_filter", state.useHxFilter);
  preferences.putBool("hx_high_stab", state.hxHighStability);
  preferences.putFloat("mass_cal", massCalibrationFactor);
  
  preferences.putInt("pin_tof_sda", pinTofSda);
  preferences.putInt("pin_tof_scl", pinTofScl);
  preferences.putInt("pin_enc_sda", pinEncSda);
  preferences.putInt("pin_enc_scl", pinEncScl);
  preferences.putInt("pin_hx_dt", pinHxDt);
  preferences.putInt("pin_hx_sck", pinHxSck);
  preferences.putInt("pin_led_rgb", pinLedRgb);
  preferences.end();
  Serial.println("[NVS] Guardado exitoso");
}

// ═══════════════════════════════════════════════════════════════
// DIAGNÓSTICO I2C
// ═══════════════════════════════════════════════════════════════
// Recuperación de bus I2C bloqueado (SDA atascado en LOW)
// Envía 9 pulsos de reloj + condición STOP para liberar el esclavo
void recoverI2C(int sda, int scl) {
  pinMode(scl, OUTPUT);
  pinMode(sda, INPUT_PULLUP);
  delayMicroseconds(10);
  // Si SDA está libre, no hay nada que recuperar
  if (digitalRead(sda) == HIGH) return;
  Serial.printf("[I2C] Bus SDA=%d atascado, recuperando...\n", sda);
  for (int i = 0; i < 9; i++) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
    if (digitalRead(sda) == HIGH) break; // SDA liberado
  }
  // Generar condición STOP
  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
  Serial.println("[I2C] Bus recuperado");
}

void scanI2C(TwoWire& bus, const char* busName) {
  Serial.printf("[I2C SCAN] %s: ", busName);
  int found = 0;
  // Solo escanear direcciones conocidas para evitar bloqueos largos
  uint8_t knownAddrs[] = {0x29, 0x36, 0x2A, 0x52, 0x6C, 0x10, 0x08};
  for (int i = 0; i < 7; i++) {
    if (validateI2CDevice(bus, knownAddrs[i])) {
      Serial.printf("0x%02X ", knownAddrs[i]);
      found++;
    }
  }
  Serial.println(found == 0 ? "ningún dispositivo" : "");
}

// ═══════════════════════════════════════════════════════════════
// INICIALIZACIÓN DE SENSORES
// ═══════════════════════════════════════════════════════════════
void initSensors() {
  // Recuperar buses I2C si están bloqueados (antes de Wire.begin)
  recoverI2C(pinTofSda, pinTofScl);
  recoverI2C(pinEncSda, pinEncScl);

  // Bus I2C #0 — ToF
  I2C_TOF.begin(pinTofSda, pinTofScl);
  I2C_TOF.setClock(400000);
  I2C_TOF.setTimeOut(100);
  scanI2C(I2C_TOF, "Bus0");

  // Bus I2C #1 — Encoder (inicializar ANTES de auto-detección)
  I2C_ENC.begin(pinEncSda, pinEncScl);
  I2C_ENC.setClock(400000);
  I2C_ENC.setTimeOut(100);
  scanI2C(I2C_ENC, "Bus1");

  // ── Auto-detección: ¿en qué bus está el ToF? ──
  tofBus = &I2C_TOF; // default Bus 0
  bool tofOnBus0 = false;
  bool tofOnBus1 = false;
  
  uint8_t targetAddr = (currentToFModel == "vi5300") ? 0x6C : 0x29;
  tofOnBus0 = validateI2CDevice(I2C_TOF, targetAddr);
  tofOnBus1 = validateI2CDevice(I2C_ENC, targetAddr);
  
  if (!tofOnBus0 && tofOnBus1) {
    tofBus = &I2C_ENC;
    Serial.printf("[I2C] ToF detectado en Bus 1 (GPIO %d/%d)\n", pinEncSda, pinEncScl);
  } else if (tofOnBus0) {
    Serial.printf("[I2C] ToF detectado en Bus 0 (GPIO %d/%d)\n", pinTofSda, pinTofScl);
  } else {
    Serial.printf("[WARN] ToF (0x%02X) no detectado en ningún bus\n", targetAddr);
  }

  state.tofReady = false;
  switch (activeToF) {
    case MODEL_L0X:
      tof0X.setBus(tofBus);
      if (tof0X.init()) {
        tof0X.setTimeout(100); // Evitar bloqueos infinitos de la librería si se desconecta
        if (state.tofRange == "long") {
            // Habilitar perfil "Long Range" para alcanzar 2 metros
            tof0X.setSignalRateLimit(0.1);
            tof0X.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
            tof0X.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);
        }

        uint32_t budgetUs = (sampleRateMs * 1000);
        if (state.tofRange == "long") {
            // Mínimo recomendado para long range en VL53L0X es 33000us para estabilidad
            if (budgetUs < 33000) budgetUs = 33000; 
        } else {
            // ST prohíbe presupuestos menores de 20ms (20000us) en cualquier perfil
            if (budgetUs < 20000) budgetUs = 20000;
        }
        
        tof0X.setMeasurementTimingBudget(budgetUs);
        // El período de inicio continuo debe ser al menos igual al presupuesto de tiempo (budgetUs / 1000)
        uint32_t periodMs = budgetUs / 1000;
        if (periodMs < (uint32_t)sampleRateMs) periodMs = sampleRateMs;
        tof0X.startContinuous(periodMs);
        state.tofReady = true;
        Serial.printf("[OK] VL53L0X (ToF) inicializado a %d ms (Presupuesto: %u us, Modo %s)\n", periodMs, budgetUs, state.tofRange == "long" ? "Long Range" : "Short Range");
      }
      break;
    case MODEL_L1X:
    case MODEL_L1X_V2:
      tof1X.setBus(tofBus);
      if (tof1X.init()) {
        tof1X.setTimeout(100); // Evitar bloqueos infinitos
        // Respetar el selector tofRange del usuario (igual que VL53L0X)
        if (state.tofRange == "short") {
          tof1X.setDistanceMode(VL53L1X::Short);  // Hasta 1.3m, inmune a luz ambiente
        } else {
          tof1X.setDistanceMode(VL53L1X::Long);   // Hasta 4m, más sensible a luz
        }
        uint32_t budgetUs = (sampleRateMs * 1000);
        // Mínimo 15ms en Short, 20ms en Long (según Pololu/ST)
        uint32_t minBudget = (state.tofRange == "short") ? 15000 : 20000;
        if (budgetUs < minBudget) budgetUs = minBudget;
        tof1X.setMeasurementTimingBudget(budgetUs);
        
        uint32_t periodMs = budgetUs / 1000;
        if (periodMs < (uint32_t)sampleRateMs) periodMs = sampleRateMs;
        tof1X.startContinuous(periodMs);
        state.tofReady = true;
        Serial.printf("[OK] VL53L1X inicializado: %d ms (Budget: %u us, Modo %s)\n",
                      periodMs, budgetUs, state.tofRange == "short" ? "Short" : "Long");
      } else {
        Serial.println("[ERR] Fallo al inicializar VL53L1X");
      }
      break;
    case MODEL_6180:
      tof6180.setBus(tofBus);
      tof6180.init();
      tof6180.configureDefault();
      state.tofReady = true;
      Serial.println("[OK] VL6180X (ToF) inicializado");
      break;
    case MODEL_L5CX:
      if (tof5CX.begin(0x29, *tofBus)) {
        tof5CX.setResolution(8 * 8);
        int freq = 1000 / sampleRateMs;
        if (freq > 15) freq = 15;
        tof5CX.setRangingFrequency(freq);
        tof5CX.startRanging();
        state.tofReady = true;
        Serial.printf("[OK] VL53L5CX (ToF) inicializado a %d Hz\n", freq);
      }
      break;
    case MODEL_TOFSENSE:
    case MODEL_TFMINI_S:
      // Estos se inicializan por UART, no por I2C (ver bloque UART abajo)
      break;
    case MODEL_TOF10120:
      state.tofReady = initToF10120(*tofBus);
      break;
    case MODEL_VI5300:
      state.tofReady = initVI5300(*tofBus);
      break;
  }

  if (!state.tofReady && activeToF != MODEL_NONE) {
    Serial.printf("[WARN] Sensor ToF %s no encontrado en Bus0\n", currentToFModel.c_str());
  }

  // AS5600 (Encoder Magnético) — inicializar solo si no está deshabilitado
  state.encoderReady = false;
  if (currentEncoderModel != "none") {
    // Detectar AS5600 (0x36) en Bus 1 primero, luego Bus 0
    activeEncoder = &encoder; // default Bus 1
    encoder.begin(255);
    if (validateI2CDevice(I2C_ENC, 0x36)) {
      state.encoderReady = true;
      Serial.printf("[OK] AS5600 (Encoder) en Bus1 (SDA:%d, SCL:%d)\n", pinEncSda, pinEncScl);
    } else {
      // Intentar en Bus 0
      encoder_alt.begin(255);
      if (validateI2CDevice(I2C_TOF, 0x36)) {
        activeEncoder = &encoder_alt;
        state.encoderReady = true;
        Serial.printf("[OK] AS5600 (Encoder) en Bus0 (SDA:%d, SCL:%d)\n", pinTofSda, pinTofScl);
      } else {
        Serial.println("[WARN] AS5600 no encontrado en ningún bus");
      }
    }
  } else {
    Serial.println("[NVS] AS5600 (Encoder) deshabilitado por configuración");
  }

  // Celda de carga / Peso
  state.loadCellReady = false;
  useNAU7802 = false;

  if (currentWeightMode == "hx711") {
    // Solo HX711
    loadCell.begin(pinHxDt, pinHxSck);
    if (loadCell.is_ready()) {
      loadCell.set_scale(massCalibrationFactor);
      loadCell.tare();
      state.loadCellReady = true;
      Serial.printf("[OK] HX711 inicializado con escala: %.3f\n", massCalibrationFactor);
    } else {
      Serial.println("[WARN] HX711 no encontrado");
    }
  } 
  else if (currentWeightMode == "nau7802") {
    // Solo NAU7802
    if (nauScale.begin(I2C_TOF)) {
      nauScale.setCalibrationFactor(massCalibrationFactor);
      nauScale.calculateZeroOffset(64);
      state.loadCellReady = true;
      useNAU7802 = true;
      Serial.printf("[OK] NAU7802 en Bus0 inicializado con escala: %.3f\n", massCalibrationFactor);
    } else if (nauScale.begin(I2C_ENC)) {
      nauScale.setCalibrationFactor(massCalibrationFactor);
      nauScale.calculateZeroOffset(64);
      state.loadCellReady = true;
      useNAU7802 = true;
      Serial.printf("[OK] NAU7802 en Bus1 inicializado con escala: %.3f\n", massCalibrationFactor);
    } else {
      Serial.println("[WARN] NAU7802 no encontrado");
    }
  }
  else {
    Serial.println("[NVS] Celdas de peso deshabilitadas por configuración");
  }

  // UART para sensores de distancia (usa pines dedicados 18/17 en perfil básico, o pines compartidos en cámara/personalizado)
  uartSensorReady = false;
  if (activeToF == MODEL_TOFSENSE || activeToF == MODEL_TFMINI_S) {
    bool isBasic = (pinTofSda == 4 && pinTofScl == 5 && pinEncSda == 10 && pinEncScl == 11 && pinHxDt == 6 && pinHxSck == 7);
    int rxPin = isBasic ? 18 : pinHxDt;
    int txPin = isBasic ? 17 : pinHxSck;
    
    bool sharedPins = (rxPin == pinHxDt || txPin == pinHxSck);
    if (sharedPins && state.loadCellReady) {
      Serial.printf("[WARN] Conflicto de pines: No se puede iniciar UART ToF porque la celda de carga está activa en los mismos pines (GPIO %d/%d).\n", pinHxDt, pinHxSck);
    } else {
      if (activeToF == MODEL_TOFSENSE) {
        // TOFSense: intentar 921600 primero, fallback a 115200
        uartSensor.begin(921600, SERIAL_8N1, rxPin, txPin);
        delay(500);
        if (!uartSensor.available()) {
          uartSensor.end();
          uartSensor.begin(115200, SERIAL_8N1, rxPin, txPin);
          Serial.println("[TOFSense] Sin datos a 921600, fallback a 115200 bps");
          delay(200);
        } else {
          Serial.println("[TOFSense] Conectado a 921600 bps");
        }
      } else {
        // TFmini-S: siempre 115200
        uartSensor.begin(115200, SERIAL_8N1, rxPin, txPin);
      }
      uartSensorReady = true;
      state.tofReady = true;
      Serial.printf("[OK] %s UART en GPIO %d/%d (RX/TX)\n",
        (activeToF == MODEL_TOFSENSE) ? "TOFSense" : "TFmini-S", rxPin, txPin);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// LECTURA DE SENSORES + CINEMÁTICA
// ═══════════════════════════════════════════════════════════════
void readSensors() {
  unsigned long now = millis();
  
  // Relajar la tasa de loop en reposo (cuando no se mide ni se espera disparo) a 50 ms (20 Hz) para dar respiro al CPU
  unsigned long activeInterval = (state.measuring || state.isWaitingForTrigger) ? sampleRateMs : 50;
  float dt = (now - state.lastSampleTime) / 1000.0; // segundos
  if (dt < (activeInterval * 0.4) / 1000.0) return; 

  static unsigned long lastTofCheck = 0;
  bool shouldCheckTof = false;
  if (state.tofReady && (now - lastTofCheck >= 20)) { // Limitar chequeo de ToF por I2C a máximo 50 Hz (cada 20 ms)
    lastTofCheck = now;
    shouldCheckTof = true;
  }

  if (state.tofReady && shouldCheckTof) {
    float dist = 0;
    bool timeout = false;
    bool newTofData = false;
    
    if (activeToF == MODEL_L0X) {
      if ((tof0X.readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) != 0) {
        dist = tof0X.readRangeContinuousMillimeters();
        timeout = tof0X.timeoutOccurred();
        newTofData = true;
      }
    } else if (activeToF == MODEL_L1X || activeToF == MODEL_L1X_V2) {
      if (tof1X.dataReady()) {
        dist = tof1X.readRangeContinuousMillimeters(false);
        timeout = tof1X.timeoutOccurred();
        // Validar calidad: solo aceptar lecturas utilizables
        uint8_t rs = tof1X.ranging_data.range_status;
        if (rs == VL53L1X::RangeValid || rs == VL53L1X::RangeValidMinRangeClipped ||
            rs == VL53L1X::RangeValidNoWrapCheckFail) {
          newTofData = true;
        }
        // Si range_status indica error (SigmaFail, SignalFail, OutOfBounds, etc.)
        // no marcamos newTofData → se conserva la última lectura válida
      }
    } else if (activeToF == MODEL_6180) {
      dist = tof6180.readRangeSingleMillimeters();
      newTofData = true;
    } else if (activeToF == MODEL_L5CX) {
      if (tof5CX.isDataReady()) {
        VL53L5CX_ResultsData data;
        if (tof5CX.getRangingData(&data)) {
          dist = data.distance_mm[0]; 
          newTofData = true;
        }
      }
    } else if (activeToF == MODEL_TOF10120) {
      float dVal = readToF10120(*tofBus);
      if (dVal >= 0) {
        dist = dVal;
        newTofData = true;
      }
    } else if (activeToF == MODEL_VI5300) {
      float dVal = readVI5300(*tofBus);
      if (dVal >= 0) {
        dist = dVal;
        newTofData = true;
      }
    } else if (uartSensorReady && (activeToF == MODEL_TOFSENSE || activeToF == MODEL_TFMINI_S)) {
      // Lectura UART de sensores de distancia
      static uint8_t uartBuf[32];
      static int uartIdx = 0;

      // TOFSense: enviar trama de consulta periódica si está en modo pasivo
      if (activeToF == MODEL_TOFSENSE) {
        static unsigned long lastTofSenseQuery = 0;
        if (now - lastTofSenseQuery >= 50) {
          lastTofSenseQuery = now;
          const uint8_t queryFrame[] = {0x57, 0x10, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x63};
          uartSensor.write(queryFrame, sizeof(queryFrame));
        }
      }

      while (uartSensor.available()) {
        uint8_t b = uartSensor.read();

        if (activeToF == MODEL_TFMINI_S) {
          // TFmini-S: Frame 9 bytes, header 0x59 0x59
          if (uartIdx == 0 && b != 0x59) continue;
          if (uartIdx == 1 && b != 0x59) { uartIdx = 0; continue; }
          uartBuf[uartIdx++] = b;
          if (uartIdx >= 9) {
            uint8_t ck = 0;
            for (int i = 0; i < 8; i++) ck += uartBuf[i];
            if (ck == uartBuf[8]) {
              uint16_t rawDist = uartBuf[2] | (uartBuf[3] << 8);
              // Algunos modelos reportan en cm, otros en mm.
              // Si el valor es < 1200 y el rango máximo es 12m (1200cm), asumimos cm y convertimos a mm.
              dist = (rawDist < 1200) ? (float)(rawDist * 10) : (float)rawDist;
              newTofData = true;
            }
            uartIdx = 0;
            break;
          }
        } else {
          // TOFSense: Frame 16 bytes, header 0x57
          if (uartIdx == 0 && b != 0x57) continue;
          uartBuf[uartIdx++] = b;
          if (uartIdx >= 16) {
            uint8_t ck = 0;
            for (int i = 0; i < 15; i++) ck += uartBuf[i];
            if (ck == uartBuf[15]) {
              uint32_t raw = uartBuf[8] | (uartBuf[9] << 8) | (uartBuf[10] << 16);
              dist = (float)raw; // mm
              newTofData = true;
            }
            uartIdx = 0;
            break;
          }
        }
      }
    }

    if (newTofData && !timeout) {
      // FILTRO ANTI-8190: Si el sensor pierde el objeto (>8000), ignorar la lectura para la cinemática.
      // Conservamos el último valor válido en la gráfica, pero contamos el glitch si estamos midiendo.
      if (dist < 8000) {
        state.prevDistance = state.lastDistance;
        state.lastDistance = dist;
        state.consecutiveGlitches = 0; // Resetear glitches al recibir dato válido
      } else {
        if (state.measuring) {
          state.consecutiveGlitches++;
        }
      }

      // Lógica de disparo y parada automática optimizada
      if (state.triggerEnabled && state.isWaitingForTrigger) {
        // Etapa de preparación: objeto posicionado cerca del sensor (entre 5.0 mm y 100.0 mm)
        if (state.lastDistance >= 5.0 && state.lastDistance <= 100.0) {
          // Si la posición es estable (variación <= 10 mm), calibramos continuamente la referencia inicial
          if (abs(state.lastDistance - state.prevDistance) <= 10.0) {
            state.initialDistance = state.lastDistance;
            setLED(CRGB::Orange); // Mantener LED naranja
          }
        }
        
        // Disparo: si tenemos una calibración estable previa y el objeto se suelta (cae alejándose, la distancia aumenta 15 mm)
        if (state.initialDistance >= 5.0 && state.initialDistance <= 100.0 && state.lastDistance >= (state.initialDistance + 15.0)) {
          state.isWaitingForTrigger = false;
          state.measuring = true;
          state.measurementStartTime = millis();
          state.tofMeasure.active = true;
          state.encMeasure.active = true;
          state.hxMeasure.active = true;
          state.consecutiveGlitches = 0;
          setLED(CRGB::Blue);
          resetBuffer();
          ws.textAll("{\"command\":\"TRIGGER_START\",\"t\":0}");
          Serial.printf("[AUTO] ¡Caída libre detectada! Disparo a %.1f mm (inicial estable: %.1f mm)\n", state.lastDistance, state.initialDistance);
        }
      } 
      else if (state.measuring) {
        bool shouldStop = false;
        
        if (state.triggerEnabled) {
          // Detener automáticamente 30 mm antes de chocar contra el suelo (suelo a tubeLength)
          if (state.tubeLength > 0 && state.lastDistance >= (state.tubeLength - 30.0)) {
            shouldStop = true;
            Serial.printf("[AUTO] Detención preventiva 30mm antes del suelo. Suelo: %.1f mm, Objeto: %.1f mm\n", state.tubeLength, state.lastDistance);
          }
          // Límite físico de seguridad
          else if (state.lastDistance >= state.distMax) {
            shouldStop = true;
            Serial.println("[AUTO] Parada por distancia máxima física alcanzada.");
          }
          // Señal perdida o fuera de rango acumulada (glitch filter: 5 lecturas consecutivas >= 8000)
          else if (state.consecutiveGlitches >= 5) {
            shouldStop = true;
            Serial.println("[AUTO] Parada por pérdida de señal sostenida (5 glitches consecutivos).");
          }
        }
        
        if (shouldStop) {
          state.measuring = false;
          state.triggerEnabled = false;
          state.tofMeasure.active = false;
          state.encMeasure.active = false;
          state.hxMeasure.active = false;
          setLED(CRGB(20, 20, 20));
          blinkLED(CRGB::Green, 3, 150);
          ws.textAll("{\"command\":\"TRIGGER_STOP\"}");
        }
      }

      // Cinemática lineal basada en tiempo de ToF real
      unsigned long tofNow = millis();
      if (state.lastTofTime > 0) {
        float tofDt = (tofNow - state.lastTofTime) / 1000.0;
        if (tofDt > 0.005) { // al menos 5 ms entre muestras de ToF
          float newVel = (state.lastDistance - state.prevDistance) / (tofDt * 1000.0); 
          state.lastAccel = (newVel - state.lastVelocity) / tofDt; 
          state.prevVelocity = state.lastVelocity;
          state.lastVelocity = newVel;
          state.lastTofTime = tofNow;
        }
      } else {
        state.lastTofTime = tofNow;
      }
    }
  }

  if (state.encoderReady) {
    int rawAngle = activeEncoder->readAngle(); 
    
    if (state.lastRawAngle == -1) {
      state.lastRawAngle = rawAngle;
    }

    int delta = rawAngle - state.lastRawAngle;
    if (delta > 2048) delta -= 4096;      
    else if (delta < -2048) delta += 4096; 

    state.lastRawAngle = rawAngle;

    float multiplier = state.invertEncoder ? -1.0 : 1.0;
    state.cumulativeAngleDeg += (delta * (360.0 / 4096.0)) * multiplier;

    state.lastAngleDeg = state.cumulativeAngleDeg;
    state.lastAngleRad = state.lastAngleDeg * DEG_TO_RAD;

    if (dt > 0 && state.lastSampleTime > 0) {
      float newAngVel = (state.lastAngleRad - state.prevAngleRad) / dt; 
      state.angularAccelRad = (newAngVel - state.prevAngularVel) / dt;  
      state.prevAngularVel = state.angularVelRad;
      state.angularVelRad = newAngVel;
    }
    state.prevAngleRad = state.lastAngleRad;
  }

  if (state.loadCellReady) {
    if (useNAU7802) {
      // Lectura NAU7802 por I2C
      if (nauScale.available()) {
        float raw = nauScale.getWeight();
        if (state.useHxFilter) {
          state.filteredWeight = (0.2f * raw) + (0.8f * state.filteredWeight);
          state.lastWeight = state.filteredWeight;
        } else {
          state.lastWeight = raw;
          state.filteredWeight = raw;
        }
      }
    } else if (loadCell.is_ready()) {
      // Lectura HX711 original
      long rawValue = loadCell.read();
      if (state.hxHighStability) {
        rawValue = (rawValue >> 8) << 8;
      }
      float raw = (float)(rawValue - loadCell.get_offset()) / loadCell.get_scale();

      if (state.useHxFilter) {
        state.filteredWeight = (0.2f * raw) + (0.8f * state.filteredWeight);
        state.lastWeight = state.filteredWeight;
      } else {
        state.lastWeight = raw;
        state.filteredWeight = raw; 
      }
    }
  }

  if (state.measuring && highSpeedBuffer) {
    highSpeedBuffer[bufferIndex] = {
      (uint32_t)(millis() - state.measurementStartTime),
      state.lastDistance,
      state.lastVelocity,
      state.lastWeight,
      state.lastAngleDeg
    };
    
    bufferIndex++;
    if (bufferIndex >= MAX_SAMPLES) {
        bufferIndex = 0;
        state.bufferFull = true;
    }
    
    if (!state.bufferFull) {
        state.samplesCount = bufferIndex;
    } else {
        state.samplesCount = MAX_SAMPLES;
    }
  }

  state.lastSampleTime = now;
}

// ═══════════════════════════════════════════════════════════════
// WEBSOCKET — STREAMING EN TIEMPO REAL
// ═══════════════════════════════════════════════════════════════
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    JsonDocument doc;
    doc["config"]["tof_model"] = currentToFModel;
    doc["config"]["enc_model"] = currentEncoderModel;
    doc["config"]["weight_mode"] = currentWeightMode;
    doc["config"]["usb_log"] = usbLogActive;
    doc["config"]["sample_rate"] = sampleRateMs;
    doc["config"]["mass_cal"] = massCalibrationFactor;
    doc["config"]["invert_encoder"] = state.invertEncoder;
    doc["config"]["hx_filter"] = state.useHxFilter;
    doc["config"]["hx_high_stab"] = state.hxHighStability;
    doc["sensors"]["tof"] = state.tofReady;
    doc["sensors"]["encoder"] = state.encoderReady;
    doc["sensors"]["loadcell"] = state.loadCellReady;
    doc["sensors"]["usb"] = usbConnected;
    doc["config"]["trigger_enabled"] = state.triggerEnabled;
    doc["config"]["tube_length"] = state.tubeLength;
    
    bool isCamProfile = (pinTofSda == 1 && pinTofScl == 47);
    doc["hardware"]["camera_detected"] = isCamProfile;
    doc["hardware"]["type"] = isCamProfile ? "freenove_cam" : "standard_base";
    doc["hardware"]["pins"]["tof_sda"] = pinTofSda;
    doc["hardware"]["pins"]["tof_scl"] = pinTofScl;
    doc["hardware"]["pins"]["enc_sda"] = pinEncSda;
    doc["hardware"]["pins"]["enc_scl"] = pinEncScl;
    doc["hardware"]["pins"]["hx_dt"] = pinHxDt;
    doc["hardware"]["pins"]["hx_sck"] = pinHxSck;
    doc["hardware"]["pins"]["led_rgb"] = pinLedRgb;
    
    String json;
    serializeJson(doc, json);
    client->text(json);
    Serial.printf("[WS] Cliente #%u conectado, configuración enviada\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Cliente #%u desconectado\n", client->id());
  } else if (type == WS_EVT_DATA) {
    String msg = String((char*)data).substring(0, len);
    if (msg == "START") {
      state.measuring = true;
      state.triggerEnabled = false;   // DESACTIVAR modo gatillo en manual
      state.isWaitingForTrigger = false;
      resetBuffer();
      state.measurementStartTime = millis();
      state.initialDistance = state.lastDistance;
      state.tofMeasure.active = true;
      state.encMeasure.active = true;
      state.hxMeasure.active = true;
      state.cumulativeAngleDeg = 0; 
      state.lastRawAngle = -1;      
      setLED(CRGB::Blue);
      ws.textAll("{\"command\":\"RESET\"}"); 
      Serial.println("[CMD] Medición global iniciada (Tiempo y Ángulo puestos a cero)");
    } else if (msg == "STOP") {
      state.measuring = false;
      state.tofMeasure.active = false;
      state.encMeasure.active = false;
      state.hxMeasure.active = false;
      setLED(CRGB(20, 20, 20));
      blinkLED(CRGB::Green, 3, 150);
      setLED(CRGB(20, 20, 20));
      Serial.println("[CMD] Medición global detenida");
    }
    else if (msg == "START_TRIGGER") {
      state.triggerEnabled = true;
      state.isWaitingForTrigger = true;
      state.initialDistance = state.lastDistance;
      state.measuring = false; 
      setLED(CRGB::Orange); 
      Serial.printf("[AUTO] Toma automática activada. Posición inicial: %.1f mm. Umbral: %.1f mm\n", state.initialDistance, state.triggerThreshold);
      ws.textAll("{\"command\":\"WAITING_TRIGGER\"}");
    }
    else if (msg.startsWith("SET_TUBE:")) {
      state.tubeLength = msg.substring(9).toFloat();
      Serial.printf("[CMD] Largo del tubo ajustado a: %.1f mm\n", state.tubeLength);
    }
    else if (msg.startsWith("SET_TOF_RANGE:")) {
      state.tofRange = msg.substring(14);
      preferences.begin("physys", false);
      preferences.putString("tof_range", state.tofRange);
      preferences.end();
      Serial.printf("[CMD] Perfil ToF ajustado a: %s (Reiniciar para aplicar)\n", state.tofRange.c_str());
    }
    else if (msg == "START_TOF") { state.initialDistance = state.lastDistance; state.tofMeasure.active = true; state.measuring = true; resetBuffer(); state.measurementStartTime = millis(); setLED(CRGB::Blue); ws.textAll("{\"command\":\"RESET\"}"); }
    else if (msg == "STOP_TOF")  { state.tofMeasure.active = false; checkGlobalStop(); }
    else if (msg == "START_ENC") { 
      state.encMeasure.active = true; 
      state.measuring = true; 
      resetBuffer();
      state.measurementStartTime = millis(); 
      state.cumulativeAngleDeg = 0; 
      state.lastRawAngle = -1;
      setLED(CRGB::Blue); 
      ws.textAll("{\"command\":\"RESET\"}"); 
      Serial.println("[CMD] Medición de Encoder iniciada (Ángulo a cero)");
    }
    else if (msg == "STOP_ENC")  { state.encMeasure.active = false; checkGlobalStop(); }
    else if (msg == "START_HX")  { state.hxMeasure.active = true; state.measuring = true; resetBuffer(); state.measurementStartTime = millis(); setLED(CRGB::Blue); ws.textAll("{\"command\":\"RESET\"}"); }
    else if (msg == "STOP_HX")   { state.hxMeasure.active = false; checkGlobalStop(); }
    else if (msg == "RESET_ENC") {
      String clientIp = client->remoteIP().toString();
      if (currentTeacherIp != clientIp && currentLeaderIp != clientIp) return; // PROTEGER
      state.cumulativeAngleDeg = 0;
      Serial.println("[CMD] Encoder puesto a cero");
    }
    else if (msg == "INVERT_ENC") {
      String clientIp = client->remoteIP().toString();
      if (currentTeacherIp != clientIp && currentLeaderIp != clientIp) return; // PROTEGER
      state.invertEncoder = !state.invertEncoder;
      saveSettings();
      ws.textAll("{\"config\":{\"invert_encoder\":" + String(state.invertEncoder ? "true" : "false") + "}}");
      Serial.printf("[CMD] Inversión de encoder: %d\n", state.invertEncoder);
    }
    else if (msg == "TOGGLE_HX_FILTER") {
      String clientIp = client->remoteIP().toString();
      if (currentTeacherIp != clientIp && currentLeaderIp != clientIp) return; // PROTEGER
      state.useHxFilter = !state.useHxFilter;
      saveSettings();
      ws.textAll("{\"config\":{\"hx_filter\":" + String(state.useHxFilter ? "true" : "false") + "}}");
      Serial.printf("[CMD] Filtro HX711: %d\n", state.useHxFilter);
    }
    else if (msg == "TOGGLE_HX_STABILITY") {
      state.hxHighStability = !state.hxHighStability;
      saveSettings();
      ws.textAll("{\"config\":{\"hx_high_stab\":" + String(state.hxHighStability ? "true" : "false") + "}}");
      Serial.printf("[CMD] Modo Alta Estabilidad HX711: %d\n", state.hxHighStability);
    }
    else if (msg == "TARE") {
      if (state.loadCellReady) {
        if (useNAU7802) {
          nauScale.calculateZeroOffset(64);
        } else {
          loadCell.tare();
        }
        Serial.println("[CMD] Celda de carga tarada");
      }
    }
    else if (msg == "SCAN_BUS") {
      // Escaneo rápido de ambos buses I2C y reporte al WebSocket
      JsonDocument scanDoc;
      scanDoc["command"] = "BUS_SCAN";
      
      // Tabla de direcciones conocidas → sensor
      struct KnownDev { uint8_t addr; const char* name; const char* mag; const char* volt; };
      KnownDev devs[] = {
        {0x29, "vl53l0x",  "distance", "3.3V"},
        {0x36, "as5600",   "angle",    "3.3V"},
        {0x2A, "nau7802",  "weight",   "3.3V"},
        {0x52, "tof10120", "distance", "3.3-5V"},
        {0x6C, "vi5300",   "distance", "3.3V"},
        {0x10, "vi5300",   "distance", "3.3V"},
      };
      int nDevs = sizeof(devs) / sizeof(devs[0]);

      for (int busIdx = 0; busIdx < 2; busIdx++) {
        TwoWire& bus = (busIdx == 0) ? I2C_TOF : I2C_ENC;
        String key = "bus" + String(busIdx);
        JsonArray arr = scanDoc[key].to<JsonArray>();
        for (int i = 0; i < nDevs; i++) {
          if (validateI2CDevice(bus, devs[i].addr)) {
            JsonObject o = arr.add<JsonObject>();
            o["addr"] = String("0x") + String(devs[i].addr, HEX);
            o["sensor"] = devs[i].name;
            o["mag"] = devs[i].mag;
            o["volt"] = devs[i].volt;
          }
        }
      }
      // Info de HX711/UART
      scanDoc["hx711"] = state.loadCellReady && !useNAU7802;
      scanDoc["uart"] = uartSensorReady;
      
      String scanJson;
      serializeJson(scanDoc, scanJson);
      client->text(scanJson);
      Serial.printf("[CMD] SCAN_BUS enviado: %s\n", scanJson.c_str());
    }
    else if (msg.startsWith("SET_TOF:")) {
      currentToFModel = msg.substring(8);
      currentToFModel.toLowerCase();
      currentToFModel.trim();
      saveSettings();
      Serial.printf("[CMD] Modelo ToF cambiado a: %s\n", currentToFModel.c_str());
    }
    else if (msg.startsWith("SET_ENC:")) {
      currentEncoderModel = msg.substring(8);
      currentEncoderModel.toLowerCase();
      currentEncoderModel.trim();
      saveSettings();
      Serial.printf("[CMD] Modelo Encoder cambiado a: %s\n", currentEncoderModel.c_str());
    }
    else if (msg.startsWith("SET_WEIGHT:")) {
      currentWeightMode = msg.substring(11);
      currentWeightMode.toLowerCase();
      currentWeightMode.trim();
      saveSettings();
      Serial.printf("[CMD] Modo de Peso cambiado a: %s\n", currentWeightMode.c_str());
    }
    else if (msg.startsWith("SET_MASS_CAL:")) {
      float newFactor = msg.substring(13).toFloat();
      if (newFactor != 0.0f) {
        massCalibrationFactor = newFactor;
        saveSettings();
        if (useNAU7802) {
          nauScale.setCalibrationFactor(massCalibrationFactor);
        } else {
          loadCell.set_scale(massCalibrationFactor);
        }
        Serial.printf("[CMD] Factor de calibración de masa cambiado a: %.3f\n", massCalibrationFactor);
        ws.textAll("{\"config\":{\"mass_cal\":" + String(massCalibrationFactor, 3) + "}}");
      }
    }
    else if (msg.startsWith("SET_RATE:")) {
      int newRate = msg.substring(9).toInt();
      if (newRate >= 2 && newRate <= 1000) {
        sampleRateMs = newRate;
        saveSettings();
        Serial.printf("[CMD] Frecuencia de muestreo cambiada a: %d ms\n", sampleRateMs);
        ws.textAll("{\"config\":{\"sample_rate\":" + String(sampleRateMs) + "}}");

        // Reconfiguración en caliente del sensor ToF activo
        if (state.tofReady) {
          if (activeToF == MODEL_L0X) {
            tof0X.stopContinuous();
            uint32_t budget = (sampleRateMs * 1000);
            if (state.tofRange == "long") {
              if (budget < 33000) budget = 33000; 
            } else {
              if (budget < 20000) budget = 20000;
            }
            tof0X.setMeasurementTimingBudget(budget);
            tof0X.startContinuous(sampleRateMs);
            Serial.printf("[HOT-CONFIG] VL53L0X reconfigurado: timing budget = %u us, rate = %d ms\n", budget, sampleRateMs);
          } else if (activeToF == MODEL_L1X || activeToF == MODEL_L1X_V2) {
            tof1X.stopContinuous();
            if (state.tofRange == "short") {
              tof1X.setDistanceMode(VL53L1X::Short);
            } else {
              tof1X.setDistanceMode(VL53L1X::Long);
            }
            uint32_t budget = (sampleRateMs * 1000);
            uint32_t minB = (state.tofRange == "short") ? 15000 : 20000;
            if (budget < minB) budget = minB;
            tof1X.setMeasurementTimingBudget(budget);
            tof1X.startContinuous(sampleRateMs);
            Serial.printf("[HOT-CONFIG] VL53L1X: budget=%u us, rate=%d ms, modo %s\n",
                          budget, sampleRateMs, state.tofRange == "short" ? "Short" : "Long");
          } else if (activeToF == MODEL_L5CX) {
            tof5CX.stopRanging();
            int freq = 1000 / sampleRateMs;
            if (freq > 15) freq = 15;
            if (freq < 1) freq = 1;
            tof5CX.setRangingFrequency(freq);
            tof5CX.startRanging();
            Serial.printf("[HOT-CONFIG] VL53L5CX reconfigurado: freq = %d Hz\n", freq);
          } else if (activeToF == MODEL_VI5300) {
            VI5300_Stop_Continuous_Measure();
            uint8_t fps;
            uint32_t intecounts;
            configureVI5300Params(fps, intecounts);
            VI5300_Set_Integralcounts_Frame(fps, intecounts);
            VI5300_Start_Continuous_Measure();
            Serial.printf("[HOT-CONFIG] VI5300 reconfigurado: %d FPS, intecounts=%u, modo %s\n", 
                          fps, intecounts, state.tofRange == "long" ? "Long" : "Short");
          }
        }
      }
    }
    else if (msg.startsWith("SET_PINS:")) {
      String clientIp = client->remoteIP().toString();
      if (currentTeacherIp != clientIp) return; // SOLO DOCENTE (Líder NO puede reasignar pines de hardware)
      
      String payload = msg.substring(9);
      if (payload.length() > 0) {
        int commaCount = 0;
        for (int i = 0; i < (int)payload.length(); i++) {
          if (payload[i] == ',') commaCount++;
        }
        
        // Formato: tofSda,tofScl,encSda,encScl,hxDt,hxSck[,ledRgb]
        int p[7];
        p[0] = pinTofSda; p[1] = pinTofScl; p[2] = pinEncSda; p[3] = pinEncScl;
        p[4] = pinHxDt; p[5] = pinHxSck; p[6] = pinLedRgb;
        
        int lastIndex = 0;
        for (int i=0; i<7; i++) {
          int commaIndex = payload.indexOf(',', lastIndex);
          if (commaIndex == -1) {
            if (lastIndex < (int)payload.length()) {
              p[i] = payload.substring(lastIndex).toInt();
            }
            break;
          } else {
            p[i] = payload.substring(lastIndex, commaIndex).toInt();
            lastIndex = commaIndex + 1;
          }
        }
        
        if (commaCount >= 5) { // Al menos 6 pines (5 comas)
          pinTofSda = p[0]; pinTofScl = p[1];
          pinEncSda = p[2]; pinEncScl = p[3];
          pinHxDt = p[4]; pinHxSck = p[5];
          if (commaCount >= 6) { // 7 pines (6 comas)
            pinLedRgb = p[6];
          }
          saveSettings();
          Serial.println("[CMD] Pines dinámicos actualizados. Reiniciando...");
          delay(500);
          ESP.restart();
        }
      }
    }
    else if (msg == "USB_EXPORT") {
      preferences.begin("physys", false);
      preferences.putString("boot_mode", "usb_export");
      preferences.end();
      ws.textAll("{\"status\":\"usb_export_starting\"}");
      Serial.println("[USB] Reiniciando para exportación...");
      delay(1000);
      ESP.restart();
    }
    else if (msg == "REBOOT") {
      Serial.println("[CMD] Comando REBOOT recibido");
      safeReboot();
    }
    else if (msg.startsWith("AUTH:")) {
      String pin = msg.substring(5);
      pin.trim();
      String clientIp = client->remoteIP().toString();
      
      if (pin == "1234") { // Leader
        if (currentLeaderIp == "" || currentLeaderIp == clientIp) {
          currentLeaderIp = clientIp;
          client->text("{\"auth\":\"leader\",\"status\":\"success\"}");
          Serial.printf("[AUTH] Líder asignado a IP: %s\n", clientIp.c_str());
        } else {
          client->text("{\"auth\":\"student\",\"status\":\"busy\",\"ip\":\"" + currentLeaderIp + "\"}");
        }
      } else if (pin == "Umng-2026") { // Teacher
        currentTeacherIp = clientIp;
        client->text("{\"auth\":\"teacher\",\"status\":\"success\"}");
        Serial.printf("[AUTH] Docente activo en IP: %s\n", clientIp.c_str());
      } else {
        client->text("{\"auth\":\"student\",\"status\":\"fail\"}");
      }
    }
    else if (msg == "DEAUTH") {
      String clientIp = client->remoteIP().toString();
      if (currentLeaderIp == clientIp) currentLeaderIp = "";
      if (currentTeacherIp == clientIp) currentTeacherIp = "";
      client->text("{\"auth\":\"student\",\"status\":\"success\"}");
      Serial.printf("[AUTH] Usuario desconectado: %s\n", clientIp.c_str());
    }
  }
}

void checkGlobalStop() {
  if (!state.tofMeasure.active && !state.encMeasure.active && !state.hxMeasure.active) {
    state.measuring = false;
    setLED(CRGB(20, 20, 20));
    blinkLED(CRGB::Green, 3, 150);
    setLED(CRGB(20, 20, 20));
  }
}

void broadcastSensorData() {
  if (ws.count() == 0 || !state.measuring) return;

  JsonDocument doc;
  doc["t"] = millis() - state.measurementStartTime; 
  doc["dist"] = state.lastDistance;            
  doc["vel"] = state.lastVelocity;             
  doc["acc"] = state.lastAccel;                
  doc["angleDeg"] = state.lastAngleDeg;        
  doc["angleRad"] = state.lastAngleRad;        
  doc["angVel"] = state.angularVelRad;         
  doc["angAcc"] = state.angularAccelRad;       
  doc["weight"] = state.lastWeight;            
  doc["mass"] = state.lastWeight / 1000.0;     
  doc["weightN"] = (state.lastWeight / 1000.0) * 9.81; 

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// ═══════════════════════════════════════════════════════════════
// API ENDPOINTS
// ═══════════════════════════════════════════════════════════════
void setupAPI() {
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["heap"] = ESP.getFreeHeap();
    doc["psram"] = ESP.getFreePsram();
    doc["uptime"] = millis() / 1000;
    doc["sensors"]["tof"] = state.tofReady;
    doc["sensors"]["encoder"] = state.encoderReady;
    doc["sensors"]["loadcell"] = state.loadCellReady;
    doc["sensors"]["usb"] = usbConnected;
    doc["measuring"] = state.measuring;
    doc["config"]["tof_model"] = currentToFModel;
    doc["config"]["enc_model"] = currentEncoderModel;
    doc["config"]["weight_mode"] = currentWeightMode;
    doc["config"]["usb_log"] = usbLogActive;
    doc["config"]["sample_rate"] = sampleRateMs;
    doc["config"]["mass_cal"] = massCalibrationFactor;
    doc["config"]["invert_encoder"] = state.invertEncoder;
    doc["config"]["hx_filter"] = state.useHxFilter;
    doc["config"]["hx_high_stab"] = state.hxHighStability;
    doc["config"]["tube_length"] = state.tubeLength;
    doc["config"]["tof_range"] = state.tofRange;
    doc["auth"]["leader_ip"] = currentLeaderIp;
    doc["auth"]["teacher_ip"] = currentTeacherIp;
    
    bool isCamProfile = (pinTofSda == 1 && pinTofScl == 47);
    doc["hardware"]["camera_detected"] = isCamProfile;
    doc["hardware"]["type"] = isCamProfile ? "freenove_cam" : "standard_base";
    doc["hardware"]["pins"]["tof_sda"] = pinTofSda;
    doc["hardware"]["pins"]["tof_scl"] = pinTofScl;
    doc["hardware"]["pins"]["enc_sda"] = pinEncSda;
    doc["hardware"]["pins"]["enc_scl"] = pinEncScl;
    doc["hardware"]["pins"]["hx_dt"] = pinHxDt;
    doc["hardware"]["pins"]["hx_sck"] = pinHxSck;
    doc["hardware"]["pins"]["led_rgb"] = pinLedRgb;
    
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (SystemFS.exists("/config.json")) {
      req->send(SystemFS, "/config.json", "application/json");
    } else {
      req->send(200, "application/json", "{\"lab_name\":\"Physys Lab — UMNG\",\"version\":\"V_1_16_07_26\"}");
    }
  });

  server.on("/api/gpio", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    struct PinInfo { int gpio; const char* label; const char* fn; };
    PinInfo allPins[] = {
      {0, "BOOT/0", "boot"}, {1, "1", "gpio"}, {2, "LED_ON/2", "strap"}, {3, "3", "gpio"}, 
      {4, "4", "gpio"}, {5, "5", "gpio"}, {6, "6", "gpio"}, {7, "7", "gpio"},
      {8, "8", "gpio"}, {9, "9", "gpio"}, {10, "10", "gpio"}, {11, "11", "gpio"},
      {12, "12", "gpio"}, {13, "13", "gpio"}, {14, "14", "gpio"}, {15, "15", "gpio"},
      {16, "16", "gpio"}, {17, "17", "gpio"}, {18, "18", "gpio"}, {19, "USB_D-", "usb"},
      {20, "USB_D+", "usb"}, {21, "21", "gpio"}, {26, "26", "gpio"}, {33, "33", "gpio"},
      {34, "34", "gpio"}, {35, "PSRAM_CLK", "psram"}, {36, "PSRAM_CS", "psram"}, {37, "PSRAM_D0", "psram"},
      {38, "38", "gpio"}, {39, "39", "gpio"}, {40, "40", "gpio"}, {41, "41", "gpio"},
      {42, "42", "gpio"}, {43, "TXD0/Debug", "uart"}, {44, "RXD0/Debug", "uart"},
      {45, "45", "strap"}, {46, "46", "strap"}, {47, "47", "gpio"}, {48, "48", "gpio"}
    };
    const int numPins = sizeof(allPins) / sizeof(allPins[0]);
    
    // Detectar si el perfil Cámara está activo en base a la asignación de pines ToF en NVS
    bool isCamProfile = (pinTofSda == 1 && pinTofScl == 47);
    
    JsonArray arr = doc["pins"].to<JsonArray>();
    for (int i = 0; i < numPins; i++) {
      JsonObject pin = arr.add<JsonObject>();
      int gpioNum = allPins[i].gpio;
      pin["g"] = gpioNum;
      pin["l"] = allPins[i].label;
      pin["f"] = allPins[i].fn;
      
      // Sobrescribir dinámicamente etiquetas del Perfil Cámara y SD Card
      if (isCamProfile) {
        if (gpioNum == 4) { pin["l"] = "CAM_SDA"; pin["f"] = "cam"; }
        else if (gpioNum == 5) { pin["l"] = "CAM_SCL"; pin["f"] = "cam"; }
        else if (gpioNum == 6) { pin["l"] = "CAM_VSYNC"; pin["f"] = "cam"; }
        else if (gpioNum == 7) { pin["l"] = "CAM_HREF"; pin["f"] = "cam"; }
        else if (gpioNum == 8) { pin["l"] = "CAM_Y4"; pin["f"] = "cam"; }
        else if (gpioNum == 9) { pin["l"] = "CAM_Y3"; pin["f"] = "cam"; }
        else if (gpioNum == 10) { pin["l"] = "CAM_Y5"; pin["f"] = "cam"; }
        else if (gpioNum == 11) { pin["l"] = "CAM_Y2"; pin["f"] = "cam"; }
        else if (gpioNum == 12) { pin["l"] = "CAM_Y6"; pin["f"] = "cam"; }
        else if (gpioNum == 13) { pin["l"] = "CAM_PCLK"; pin["f"] = "cam"; }
        else if (gpioNum == 15) { pin["l"] = "CAM_XCLK"; pin["f"] = "cam"; }
        else if (gpioNum == 16) { pin["l"] = "CAM_Y9"; pin["f"] = "cam"; }
        else if (gpioNum == 17) { pin["l"] = "CAM_Y8"; pin["f"] = "cam"; }
        else if (gpioNum == 18) { pin["l"] = "CAM_Y7"; pin["f"] = "cam"; }
        else if (gpioNum == 38) { pin["l"] = "SD_CMD"; pin["f"] = "sd"; }
        else if (gpioNum == 39) { pin["l"] = "SD_CLK"; pin["f"] = "sd"; }
        else if (gpioNum == 40) { pin["l"] = "SD_DATA"; pin["f"] = "sd"; }
      }
      
      // Sobrescribir labels si el pin está asignado dinámicamente a algún sensor
      if(gpioNum == pinTofSda) { pin["l"] = "TOF_SDA"; pin["f"] = "i2c"; }
      else if(gpioNum == pinTofScl) { pin["l"] = "TOF_SCL"; pin["f"] = "i2c"; }
      else if(gpioNum == pinEncSda) { pin["l"] = "ENC_SDA"; pin["f"] = "i2c"; }
      else if(gpioNum == pinEncScl) { pin["l"] = "ENC_SCL"; pin["f"] = "i2c"; }
      else if(gpioNum == pinHxDt) { pin["l"] = "HX_DT"; pin["f"] = "serial"; }
      else if(gpioNum == pinHxSck) { pin["l"] = "HX_SCK"; pin["f"] = "serial"; }
      
      // Mapear dinámicamente el LED RGB compilado actual
      if (gpioNum == pinLedRgb) { pin["l"] = "LED_RGB"; pin["f"] = "led"; }
      
      // Lógica Failsafe: Bloquear digitalRead() en pines críticos de PSRAM, USB, UART, Strapping, Cámara o SD activa
      bool isSensitive = (gpioNum == 35 || gpioNum == 36 || gpioNum == 37 || // PSRAM
                          gpioNum == 19 || gpioNum == 20 ||                  // USB OTG
                          gpioNum == 43 || gpioNum == 44 ||                  // UART
                          gpioNum == 0  || gpioNum == 45 || gpioNum == 46 ||  // Strapping
                          (isCamProfile && (gpioNum == 4 || gpioNum == 5 || gpioNum == 6 || 
                                            gpioNum == 7 || gpioNum == 8 || gpioNum == 9 || 
                                            gpioNum == 10 || gpioNum == 11 || gpioNum == 12 || 
                                            gpioNum == 13 || gpioNum == 15 || gpioNum == 16 || 
                                            gpioNum == 17 || gpioNum == 18 || gpioNum == 38 || 
                                            gpioNum == 39 || gpioNum == 40)));
      
      if (isSensitive) {
        // Pines de PSRAM o USB nativo usualmente leen HIGH, pero retornamos estado lógico 0 estático por seguridad
        pin["v"] = 0; 
      } else {
        pin["v"] = digitalRead(gpioNum);
      }
    }
    doc["heap"] = ESP.getFreeHeap();
    doc["t"] = millis();
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/data", HTTP_POST, [](AsyncWebServerRequest *req) {},
    NULL, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      static File uploadFile;
      static FILE* usbUploadFile = nullptr;
      static String lastFilename;

      if (index == 0) {
        lastFilename = "/exp_" + String(millis()) + ".json";
        uploadFile = DataFS.open(lastFilename, "w");
        
        if (usbConnected) {
            String usbPath = "/usb" + lastFilename;
            usbUploadFile = fopen(usbPath.c_str(), "w");
            if (usbUploadFile) {
                Serial.printf("[USB] Iniciando guardado en pendrive: %s\n", usbPath.c_str());
            } else {
                Serial.println("[USB] Error al crear archivo en pendrive");
            }
        }
        Serial.printf("[DATA] Iniciando guardado: %s (%d bytes total)\n", lastFilename.c_str(), total);
      }

      if (uploadFile) {
        uploadFile.write(data, len);
      }
      
      if (usbUploadFile) {
        fwrite(data, 1, len, usbUploadFile);
      }

      if (index + len == total) {
        if (uploadFile) uploadFile.close();
        
        if (usbUploadFile) {
            fclose(usbUploadFile);
            usbUploadFile = nullptr;
            Serial.printf("[USB] Guardado en pendrive completo.\n");
        }
        
        req->send(200, "application/json", "{\"ok\":true,\"file\":\"" + lastFilename + "\"}");
        blinkLED(CRGB::Green, 3, 150);
        Serial.printf("[DATA] Guardado completo: %s\n", lastFilename.c_str());
      }
  });

  server.on("/api/export", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    JsonArray experiments = doc["experiments"].to<JsonArray>();
    File root = DataFS.open("/");
    File file = root.openNextFile();
    while (file) {
      if (String(file.name()).endsWith(".json")) {
        JsonDocument expDoc;
        deserializeJson(expDoc, file);
        experiments.add(expDoc);
      }
      file = root.openNextFile();
    }
    doc["device"] = "Physys-Lab";
    doc["exported"] = millis();
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/data/list", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    JsonArray files = doc["files"].to<JsonArray>();
    File root = DataFS.open("/");
    File file = root.openNextFile();
    while (file) {
      JsonObject f = files.add<JsonObject>();
      f["name"] = String(file.name());
      f["size"] = file.size();
      file = root.openNextFile();
    }
    doc["total"] = DataFS.totalBytes();
    doc["used"] = DataFS.usedBytes();
    doc["free"] = DataFS.totalBytes() - DataFS.usedBytes();
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/data/clear", HTTP_DELETE, [](AsyncWebServerRequest *req) {
    int count = 0;
    File root = DataFS.open("/");
    File file = root.openNextFile();
    while (file) {
      String fname = String("/") + file.name();
      file = root.openNextFile();
      DataFS.remove(fname);
      count++;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["deleted"] = count;
    doc["free"] = DataFS.totalBytes() - DataFS.usedBytes();
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
    blinkLED(CRGB::Yellow, 2, 200);
    setLED(CRGB(20, 20, 20));
    Serial.printf("[DATA] Limpieza: %d archivos eliminados\n", count);
  });

  server.on("/api/python", HTTP_GET, [](AsyncWebServerRequest *req) {
    File f = SystemFS.open("/sensor_logic.py", "r");
    if (f) {
      req->send(SystemFS, "/sensor_logic.py", "text/plain");
    } else {
      req->send(404, "text/plain", "# No hay script guardado aun");
    }
  });

  server.on("/api/python", HTTP_POST, [](AsyncWebServerRequest *req) {},
    NULL, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      File f = SystemFS.open("/sensor_logic.py", "w");
      if (f) {
        f.write(data, len);
        f.close();
        req->send(200, "application/json", "{\"ok\":true}");
        Serial.printf("[PYTHON] Script actualizado (%d bytes)\n", len);
      } else {
        req->send(500, "application/json", "{\"ok\":false}");
        setLED(CRGB(255, 0, 255));
      }
  });

  server.on("/api/storage", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["system"]["total"] = SystemFS.totalBytes();
    doc["system"]["used"] = SystemFS.usedBytes();
    doc["data"]["total"] = DataFS.totalBytes();
    doc["data"]["used"] = DataFS.usedBytes();
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  // ── Endpoint de exportación temporal (para descarga en portal cautivo) ──
  // El cliente POST envía el contenido del archivo, el GET lo sirve como descarga
  static String tempExportData;
  static String tempExportName;
  static String tempExportMime;

  server.on("/api/temp-export", HTTP_POST, [](AsyncWebServerRequest *req) {},
    NULL, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        tempExportData = "";
        tempExportData.reserve(total + 1);
        tempExportName = req->hasHeader("X-Filename") ? req->header("X-Filename") : "export.csv";
        tempExportMime = req->hasHeader("X-Mime") ? req->header("X-Mime") : "text/csv";
      }
      for (size_t i = 0; i < len; i++) {
        tempExportData += (char)data[i];
      }
      if (index + len == total) {
        Serial.printf("[EXPORT] Archivo temporal listo: %s (%d bytes)\n", tempExportName.c_str(), total);
        req->send(200, "application/json", "{\"ok\":true,\"size\":" + String(total) + "}");
      }
  });

  server.on("/api/temp-export", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (tempExportData.length() == 0) {
      req->send(404, "text/plain", "No hay datos. Exporta primero.");
      return;
    }
    AsyncWebServerResponse *resp = req->beginResponse(200, tempExportMime, tempExportData);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + tempExportName + "\"");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    req->send(resp);
    Serial.printf("[EXPORT] Descarga servida: %s\n", tempExportName.c_str());
  });
}

// ═══════════════════════════════════════════════════════════════
// FACTORY RESET
// ═══════════════════════════════════════════════════════════════
unsigned long resetPressStart = 0;

void checkFactoryReset() {
  if (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (resetPressStart == 0) {
      resetPressStart = millis();
    } else if (millis() - resetPressStart > 10000) {
      Serial.println("[RESET] Factory Reset activado — Limpiando datos...");
      setLED(CRGB::Red);
      File root = DataFS.open("/");
      File file = root.openNextFile();
      while (file) {
        DataFS.remove(String("/") + file.name());
        file = root.openNextFile();
      }
      delay(1000);
      ESP.restart();
    }
  } else {
    resetPressStart = 0;
  }
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
// --- MODO USB HÍBRIDO ---
void runUsbExportMode() {
  setLED(CRGB::Yellow);
  Serial.println("[USB-MODE] Entrando en modo exportación...");
  
  if (!DataFS.begin(false, "/data", 10, "userdata")) {
    Serial.println("[USB-MODE] Error montando DataFS");
    blinkLED(CRGB::Red, 5, 200);
    return;
  }

  unsigned long start = millis();
  bool connected = false;
  while (millis() - start < 30000) { 
    if (usbHost.isConnected()) {
      connected = true;
      break;
    }
    blinkLED(CRGB::Blue, 1, 500);
    Serial.println("[USB-MODE] Esperando pendrive...");
  }

  if (!connected) {
    Serial.println("[USB-MODE] Timeout: No se detectó pendrive");
    blinkLED(CRGB::Red, 3, 500);
    return;
  }

  setLED(CRGB::Blue);
  Serial.println("[USB-MODE] Pendrive detectado. Copiando archivos...");

  File root = DataFS.open("/");
  File file = root.openNextFile();
  int count = 0;
  
  while (file) {
    if (!file.isDirectory()) {
      String fileName = String(file.name());
      if (fileName.startsWith("exp_")) {
        Serial.printf("[USB-MODE] Copiando %s...\n", fileName.c_str());
        
        String usbPath = "/usb/" + fileName;
        FILE* fTo = fopen(usbPath.c_str(), "w");
        if (fTo) {
          uint8_t buf[512];
          while (file.available()) {
            size_t n = file.read(buf, sizeof(buf));
            fwrite(buf, 1, n, fTo);
          }
          fclose(fTo);
          count++;
        }
      }
    }
    file = root.openNextFile();
  }

  Serial.printf("[USB-MODE] Exportación completa: %d archivos.\n", count);
  blinkLED(CRGB::Green, 3, 300);
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  
  // Cargar configuraciones del NVS al inicio para conocer los pines antes de inicializar hardware
  loadSettings();
  
  preferences.begin("physys", false);
  String bootMode = preferences.getString("boot_mode", "normal");
  
  if (bootMode == "usb_export") {
    preferences.putString("boot_mode", "normal");
    preferences.end();
    
    initFastLED(pinLedRgb);
    
    // Inicializar PSRAM para buffer de alta velocidad
    if (psramInit()) {
      highSpeedBuffer = (DataPoint*)ps_malloc(MAX_SAMPLES * sizeof(DataPoint));
      if (highSpeedBuffer) {
        Serial.printf("[PSRAM] Buffer de %d muestras reservado (%d KB)\n", 
                      MAX_SAMPLES, (MAX_SAMPLES * sizeof(DataPoint)) / 1024);
      }
    }
    
    usbHost.begin();
    
    runUsbExportMode();
    
    Serial.println("[USB-MODE] Reiniciando a modo normal...");
    ESP.restart();
  }
  preferences.end();

  // LED de estado
  initFastLED(pinLedRgb);
  FastLED.setBrightness(30);

  // Botón Factory Reset
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);

  // Montar particiones LittleFS
  Serial.println("\n[FS] Montando particiones...");
  if (SystemFS.begin(true, "/system", 10, "storage")) {
    Serial.printf("[FS] Sistema (WebApp): %d KB usados / %d KB total\n",
                  SystemFS.usedBytes() / 1024, SystemFS.totalBytes() / 1024);
  } else {
    Serial.println("[FS] ERROR: No se pudo montar partición SISTEMA");
  }

  if (DataFS.begin(false, "/data", 10, "userdata")) {
    Serial.println("[FS] Partición DATOS montada");
  } else {
    Serial.println("[FS] ERROR: No se pudo montar partición DATOS");
  }

  // Configuración persistente ya cargada al inicio de setup()

  WiFi.mode(WIFI_AP);
  
  // Generar nombre con MAC después de activar el modo WiFi
  String macSuffix = WiFi.softAPmacAddress().substring(12);
  macSuffix.replace(":", "");
  String apName = "Physys-Lab-" + macSuffix;
  
  WiFi.softAP(apName.c_str());
  delay(100);
  Serial.printf("[WiFi] AP '%s' activo en %s\n",
                apName.c_str(), WiFi.softAPIP().toString().c_str());

  // mDNS para facilitar acceso local
  if (MDNS.begin("physyslab")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] Respondiendo en http://physyslab.local");
  }

  // Portal Cautivo — DNS wildcard (obligatorio para detección de portal cautivo)
  // NOTA: Esto redirige TODOS los dominios a esta IP.
  // Para acceder a sitios externos (Desmos), desconectar WiFi o usar datos móviles.
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.println("[DNS] Portal cautivo activo");

  // Si no se inicializó en modo USB, intentar aquí
  if (!highSpeedBuffer && psramInit()) {
    highSpeedBuffer = (DataPoint*)ps_malloc(MAX_SAMPLES * sizeof(DataPoint));
    if (highSpeedBuffer) {
      Serial.printf("[PSRAM] Buffer de alta velocidad listo: %d KB\n", (MAX_SAMPLES * sizeof(DataPoint)) / 1024);
    }
  }

  // USB Host MSC (Modo Híbrido - Oficial Espressif)
  if (usbHost.begin()) {
    Serial.println("[USB] Stack oficial de Espressif inicializado.");
  } else {
    Serial.println("[USB] No se pudo inicializar el stack de Host (¿Conflicto con CDC?)");
  }
  usbConnected = false; 

  // Sensores
  Serial.println("\n[Sensores] Inicializando...");
  initSensors();

  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // API REST
  setupAPI();

  // Servir WebApp desde partición Sistema
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(SystemFS, "/www/index.html", "text/html");
  });
  // Portal cautivo: redirigir cualquier host desconocido
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://physyslab.local/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://physyslab.local/");
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://physyslab.local/");
  });

  // Archivos estáticos (CSS, JS, fuentes)
  server.serveStatic("/", SystemFS, "/www/").setDefaultFile("index.html");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
  Serial.println("\n[HTTP] Servidor activo en puerto 80");

  // Listo
  setLED(CRGB(20, 20, 20)); // Blanco opaco = Sistema listo
  state.measurementStartTime = millis();
  Serial.println("\n✓ Physys Lab listo. Conecta a WiFi 'Physys-Lab' → http://physyslab.local");
}

// ═══════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════
unsigned long lastBroadcast = 0;

void loop() {
  dnsServer.processNextRequest();
  
  // Limitar cleanupClients a 1 vez por segundo para evitar saturar la CPU y la red
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 1000) {
    ws.cleanupClients();
    lastCleanup = millis();
  }
  
  checkFactoryReset();
  usbConnected = usbHost.isConnected();

  readSensors();

  // Broadcast datos según la frecuencia configurada
  if (millis() - lastBroadcast > sampleRateMs) {
    broadcastSensorData();
    
    // El log a USB ahora se hará mediante la función de exportación
    // para evitar conflictos de hardware en tiempo real.
    
    lastBroadcast = millis();
  }
}
