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
#include <FastLED.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "UsbHostMSC.h" // Wrapper de la librería chegewara

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
#define TOF_SDA        4    // GPIO4
#define TOF_SCL        5    // GPIO5
// Bus I2C #1 — AS5600 Encoder (dirección 0x36)
#define ENC_SDA        10   // GPIO10
#define ENC_SCL        11   // GPIO11
#define HX711_DT       6    // GPIO6 — Celda de carga
#define HX711_SCK      7    // GPIO7
#define LED_PIN        48   // GPIO48 — WS2812 onboard (Freenove)
#define NUM_LEDS       1
#define FACTORY_RESET_PIN 0 // GPIO0 — BOOT button = Factory Reset (10s hold)

// ═══════════════════════════════════════════════════════════════
// OBJETOS GLOBALES
// ═══════════════════════════════════════════════════════════════
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;

TwoWire I2C_TOF = TwoWire(0);  // Bus 0 para ToF
TwoWire I2C_ENC = TwoWire(1);  // Bus 1 para Encoder

// Objetos de sensores (se inicializará el que corresponda)
VL53L0X  tof0X;
VL53L1X  tof1X;
VL6180X  tof6180;
SparkFun_VL53L5CX tof5CX;

enum ToFModel { MODEL_L0X, MODEL_L1X, MODEL_L1X_V2, MODEL_6180, MODEL_L5CX };
ToFModel activeToF = MODEL_L0X;
String currentToFModel = "vl53l0x";
int sampleRateMs = 10; // Frecuencia por defecto: 100 Hz (10 ms)

AS5600 encoder(&I2C_ENC);  // AS5600 en Bus 1
HX711 loadCell;
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
  
  unsigned long lastSampleTime = 0;
  unsigned long measurementStartTime = 0;
} state;

// Delay para asegurar guardado en NVS antes de reboot
void safeReboot() {
  Serial.println("[SYS] Preparando reinicio... Guardando buffers...");
  delay(1000); // 1 segundo de seguridad
  ESP.restart();
}

// Forward declarations
void checkGlobalStop();

// ═══════════════════════════════════════════════════════════════
// SEMÁFORO LED (RMT via FastLED)
// ═══════════════════════════════════════════════════════════════
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
// CONFIGURACIÓN PERSISTENTE
// ═══════════════════════════════════════════════════════════════
void loadSettings() {
  preferences.begin("physys", true);
  currentToFModel = preferences.getString("tof_model", "vl53l0x");
  usbLogActive = preferences.getBool("usb_log", false);
  sampleRateMs = preferences.getUInt("rate", 10); // Default 100Hz (10ms)
  state.invertEncoder = preferences.getBool("inv_enc", false);
  state.useHxFilter = preferences.getBool("hx_filter", true);
  state.hxHighStability = preferences.getBool("hx_high_stab", false);
  preferences.end();
  
  // Validaciones de seguridad - Forzar 10ms (100Hz) si no es válido o es la primera vez
  if (sampleRateMs < 2 || sampleRateMs > 1000) sampleRateMs = 10;
  if (currentToFModel == "" || currentToFModel.length() < 3) currentToFModel = "vl53l0x";

  Serial.printf("[NVS] Configuración cargada: ToF=%s, USB_Log=%d, Rate=%d ms (%d Hz)\n", 
                currentToFModel.c_str(), usbLogActive, sampleRateMs, 1000/sampleRateMs);
  
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
  } else {
    activeToF = MODEL_L0X;
    state.distMin = 30;
    state.distMax = 2000;
  }
}

void saveSettings() {
  Serial.printf("[NVS] Guardando: ToF=%s, Rate=%d ms, InvEnc=%d\n", currentToFModel.c_str(), sampleRateMs, state.invertEncoder);
  if (!preferences.begin("physys", false)) {
    Serial.println("[ERR] No se pudo abrir NVS para escritura");
    return;
  }
  preferences.putString("tof_model", currentToFModel);
  preferences.putBool("usb_log", usbLogActive);
  preferences.putInt("sample_rate", sampleRateMs);
  preferences.putBool("inv_enc", state.invertEncoder);
  preferences.putBool("hx_filter", state.useHxFilter);
  preferences.putBool("hx_high_stab", state.hxHighStability);
  preferences.end();
  Serial.println("[NVS] Guardado exitoso");
}

// ═══════════════════════════════════════════════════════════════
// INICIALIZACIÓN DE SENSORES
// ═══════════════════════════════════════════════════════════════
void initSensors() {
  // Bus I2C #0 — ToF (Varios modelos)
  I2C_TOF.begin(TOF_SDA, TOF_SCL);
  I2C_TOF.setClock(400000);

  state.tofReady = false;
  switch (activeToF) {
    case MODEL_L0X:
      tof0X.setBus(&I2C_TOF);
      if (tof0X.init()) {
        uint32_t budget = (sampleRateMs * 1000) - 2000;
        if (budget < 10000) budget = 10000; 
        tof0X.setMeasurementTimingBudget(budget);
        tof0X.startContinuous(sampleRateMs);
        state.tofReady = true;
        Serial.printf("[OK] VL53L0X (ToF) inicializado a %d ms\n", sampleRateMs);
      }
      break;
    case MODEL_L1X:
    case MODEL_L1X_V2:
      tof1X.setBus(&I2C_TOF);
      if (tof1X.init()) {
        if (sampleRateMs <= 20) {
          tof1X.setDistanceMode(VL53L1X::Short);
        } else {
          tof1X.setDistanceMode(VL53L1X::Long);
        }
        uint32_t budget = (sampleRateMs * 1000);
        if (budget < 15000) budget = 15000; 
        tof1X.setMeasurementTimingBudget(budget);
        tof1X.startContinuous(sampleRateMs);
        state.tofReady = true;
        Serial.printf("[OK] VL53L1X inicializado a %d ms (Budget: %d us)\n", sampleRateMs, budget);
      } else {
        Serial.println("[ERR] Fallo al inicializar VL53L1X");
      }
      break;
    case MODEL_6180:
      tof6180.setBus(&I2C_TOF);
      tof6180.init();
      tof6180.configureDefault();
      state.tofReady = true;
      Serial.println("[OK] VL6180X (ToF) inicializado");
      break;
    case MODEL_L5CX:
      if (tof5CX.begin(0x29, I2C_TOF)) {
        tof5CX.setResolution(8 * 8);
        int freq = 1000 / sampleRateMs;
        if (freq > 15) freq = 15;
        tof5CX.setRangingFrequency(freq);
        tof5CX.startRanging();
        state.tofReady = true;
        Serial.printf("[OK] VL53L5CX (ToF) inicializado a %d Hz\n", freq);
      }
      break;
  }

  if (!state.tofReady) {
    Serial.printf("[WARN] Sensor ToF %s no encontrado en Bus0\n", currentToFModel.c_str());
  }

  // AS5600 (Encoder Magnético) en Bus 1
  I2C_ENC.begin(ENC_SDA, ENC_SCL);
  I2C_ENC.setClock(400000);
  encoder.begin(255); 
  if (encoder.isConnected()) {
    state.encoderReady = true;
    Serial.println("[OK] AS5600 (Encoder) en Bus1 GPIO10/11");
  } else {
    Serial.println("[WARN] AS5600 no encontrado en Bus1");
  }

  // HX711 (Celda de Carga)
  loadCell.begin(HX711_DT, HX711_SCK);
  if (loadCell.is_ready()) {
    loadCell.set_scale(420.0);
    loadCell.tare();
    state.loadCellReady = true;
    Serial.println("[OK] HX711 inicializado");
  } else {
    Serial.println("[WARN] HX711 no encontrado");
  }
}

// ═══════════════════════════════════════════════════════════════
// LECTURA DE SENSORES + CINEMÁTICA
// ═══════════════════════════════════════════════════════════════
void readSensors() {
  unsigned long now = millis();
  float dt = (now - state.lastSampleTime) / 1000.0; // segundos
  if (dt < (sampleRateMs * 0.4) / 1000.0) return; 

  if (state.tofReady) {
    float dist = 0;
    bool timeout = false;
    
    if (activeToF == MODEL_L0X) {
      dist = tof0X.readRangeContinuousMillimeters();
      timeout = tof0X.timeoutOccurred();
    } else if (activeToF == MODEL_L1X || activeToF == MODEL_L1X_V2) {
      dist = tof1X.readRangeContinuousMillimeters();
      timeout = tof1X.timeoutOccurred();
    } else if (activeToF == MODEL_6180) {
      dist = tof6180.readRangeSingleMillimeters();
    } else if (activeToF == MODEL_L5CX) {
      if (tof5CX.isDataReady()) {
        VL53L5CX_ResultsData data;
        if (tof5CX.getRangingData(&data)) {
          dist = data.distance_mm[0]; 
        }
      }
    }

    if (!timeout && dist > 0 && dist < 8000) {
      state.prevDistance = state.lastDistance;
      state.lastDistance = dist;

      if (state.triggerEnabled && state.isWaitingForTrigger) {
        if (abs(state.lastDistance - state.initialDistance) >= state.triggerThreshold) {
          state.isWaitingForTrigger = false;
          state.measuring = true;
          state.measurementStartTime = millis();
          state.tofMeasure.active = true;
          state.encMeasure.active = true;
          state.hxMeasure.active = true;
          setLED(CRGB::Blue);
          bufferIndex = 0; // Reset buffer para nueva captura
          ws.textAll("{\"command\":\"TRIGGER_START\",\"t\":0}");
          Serial.println("[AUTO] ¡Movimiento detectado! Iniciando registro automático.");
        }
      } 
      else if (state.triggerEnabled && state.measuring && state.tubeLength > 0) {
        if (state.lastDistance >= state.tubeLength) {
          state.measuring = false;
          state.triggerEnabled = false;
          state.tofMeasure.active = false;
          state.encMeasure.active = false;
          state.hxMeasure.active = false;
          setLED(CRGB(20, 20, 20));
          blinkLED(CRGB::Green, 3, 150);
          setLED(CRGB(20, 20, 20));
          ws.textAll("{\"command\":\"TRIGGER_STOP\"}");
          Serial.printf("[AUTO] Fin de carrera alcanzado (%.1f mm). Deteniendo registro.\n", state.lastDistance);
        }
      }

      if (dt > 0 && state.lastSampleTime > 0) {
        float newVel = (state.lastDistance - state.prevDistance) / (dt * 1000.0); 
        state.lastAccel = (newVel - state.lastVelocity) / dt; 
        state.prevVelocity = state.lastVelocity;
        state.lastVelocity = newVel;
      }

      if (state.measuring && highSpeedBuffer && bufferIndex < MAX_SAMPLES) {
        highSpeedBuffer[bufferIndex++] = {
          (uint32_t)(millis() - state.measurementStartTime),
          state.lastDistance,
          state.lastVelocity,
          state.lastWeight,
          state.lastAngleDeg
        };
      }
    }
  }

  if (state.encoderReady) {
    int rawAngle = encoder.readAngle(); 
    
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

  if (state.loadCellReady && loadCell.is_ready()) {
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
    doc["config"]["usb_log"] = usbLogActive;
    doc["config"]["sample_rate"] = sampleRateMs;
    doc["config"]["invert_encoder"] = state.invertEncoder;
    doc["config"]["hx_filter"] = state.useHxFilter;
    doc["config"]["hx_high_stab"] = state.hxHighStability;
    doc["sensors"]["tof"] = state.tofReady;
    doc["sensors"]["encoder"] = state.encoderReady;
    doc["sensors"]["loadcell"] = state.loadCellReady;
    doc["sensors"]["usb"] = usbConnected;
    doc["config"]["trigger_enabled"] = state.triggerEnabled;
    doc["config"]["tube_length"] = state.tubeLength;
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
      state.measurementStartTime = millis();
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
    else if (msg == "START_TOF") { state.tofMeasure.active = true; state.measuring = true; state.measurementStartTime = millis(); setLED(CRGB::Blue); ws.textAll("{\"command\":\"RESET\"}"); }
    else if (msg == "STOP_TOF")  { state.tofMeasure.active = false; checkGlobalStop(); }
    else if (msg == "START_ENC") { 
      state.encMeasure.active = true; 
      state.measuring = true; 
      state.measurementStartTime = millis(); 
      state.cumulativeAngleDeg = 0; 
      state.lastRawAngle = -1;
      setLED(CRGB::Blue); 
      ws.textAll("{\"command\":\"RESET\"}"); 
      Serial.println("[CMD] Medición de Encoder iniciada (Ángulo a cero)");
    }
    else if (msg == "STOP_ENC")  { state.encMeasure.active = false; checkGlobalStop(); }
    else if (msg == "START_HX")  { state.hxMeasure.active = true; state.measuring = true; state.measurementStartTime = millis(); setLED(CRGB::Blue); ws.textAll("{\"command\":\"RESET\"}"); }
    else if (msg == "STOP_HX")   { state.hxMeasure.active = false; checkGlobalStop(); }
    else if (msg == "RESET_ENC") {
      state.cumulativeAngleDeg = 0;
      Serial.println("[CMD] Encoder puesto a cero");
    }
    else if (msg == "INVERT_ENC") {
      state.invertEncoder = !state.invertEncoder;
      saveSettings();
      ws.textAll("{\"config\":{\"invert_encoder\":" + String(state.invertEncoder ? "true" : "false") + "}}");
      Serial.printf("[CMD] Inversión de encoder: %d\n", state.invertEncoder);
    }
    else if (msg == "TOGGLE_HX_FILTER") {
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
        loadCell.tare();
        Serial.println("[CMD] Celda de carga tarada");
      }
    }
    else if (msg.startsWith("SET_TOF:")) {
      currentToFModel = msg.substring(8);
      currentToFModel.toLowerCase();
      currentToFModel.trim();
      saveSettings();
      Serial.printf("[CMD] Modelo ToF cambiado a: %s (Aplicará tras reinicio)\n", currentToFModel.c_str());
    }
    else if (msg.startsWith("SET_RATE:")) {
      int newRate = msg.substring(9).toInt();
      if (newRate >= 2 && newRate <= 1000) {
        sampleRateMs = newRate;
        saveSettings();
        Serial.printf("[CMD] Frecuencia de muestreo cambiada a: %d ms\n", sampleRateMs);
        ws.textAll("{\"config\":{\"sample_rate\":" + String(sampleRateMs) + "}}");
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
  static unsigned long lastBroadcast = 0;
  unsigned long now = millis();
  
  if (ws.count() == 0 || !state.measuring || (now - lastBroadcast < 33)) return;
  lastBroadcast = now;

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
  doc["sensors"]["tof"] = state.tofReady;
  doc["sensors"]["encoder"] = state.encoderReady;
  doc["sensors"]["loadcell"] = state.loadCellReady;
  doc["sensors"]["usb"] = usbConnected;
  doc["config"]["trigger_enabled"] = state.triggerEnabled;
  doc["config"]["waiting_trigger"] = state.isWaitingForTrigger;
  doc["config"]["tof_model"] = currentToFModel;
  doc["config"]["usb_log"] = usbLogActive;
  doc["config"]["sample_rate"] = sampleRateMs;
  doc["config"]["hx_filter"] = state.useHxFilter;
  doc["config"]["hx_high_stab"] = state.hxHighStability;

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
    doc["config"]["usb_log"] = usbLogActive;
    doc["config"]["sample_rate"] = sampleRateMs;
    doc["config"]["invert_encoder"] = state.invertEncoder;
    doc["config"]["hx_filter"] = state.useHxFilter;
    doc["config"]["hx_high_stab"] = state.hxHighStability;
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (SystemFS.exists("/config.json")) {
      req->send(SystemFS, "/config.json", "application/json");
    } else {
      req->send(200, "application/json", "{\"lab_name\":\"Physys Lab — UMNG\",\"version\":\"v8.0\"}");
    }
  });

  server.on("/api/gpio", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    struct PinInfo { int gpio; const char* label; const char* fn; };
    PinInfo allPins[] = {
      {0,  "BOOT",     "boot"},
      {4,  "TOF_SDA",  "i2c"},
      {5,  "TOF_SCL",  "i2c"},
      {6,  "HX_DT",    "serial"},
      {7,  "HX_SCK",   "serial"},
      {10, "ENC_SDA",  "i2c"},
      {11, "ENC_SCL",  "i2c"},
      {48, "WS2812",   "led"}
    };
    const int numPins = sizeof(allPins) / sizeof(allPins[0]);
    
    JsonArray arr = doc["pins"].to<JsonArray>();
    for (int i = 0; i < numPins; i++) {
      JsonObject pin = arr.add<JsonObject>();
      pin["g"] = allPins[i].gpio;
      pin["l"] = allPins[i].label;
      pin["f"] = allPins[i].fn;
      pin["v"] = digitalRead(allPins[i].gpio);
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
  
  preferences.begin("physys", false);
  String bootMode = preferences.getString("boot_mode", "normal");
  
  if (bootMode == "usb_export") {
    preferences.putString("boot_mode", "normal");
    preferences.end();
    
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    
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
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
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

  // Configuración persistente
  loadSettings();

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

  // Portal Cautivo — DNS wildcard
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
  ws.cleanupClients();
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
