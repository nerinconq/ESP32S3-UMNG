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
int pinTofSda = 4;
int pinTofScl = 5;
// Bus I2C #1 — AS5600 Encoder (dirección 0x36)
int pinEncSda = 10;
int pinEncScl = 11;
int pinHxDt = 6;
int pinHxSck = 7;
#define LED_PIN        48   // GPIO48 — WS2812 onboard (Freenove)
#define NUM_LEDS       1
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
  String tofRange = "short";    // Perfil VL53L0X: "short" o "long"
  
  uint32_t samplesCount = 0;
  bool bufferFull = false;
  unsigned long lastSampleTime = 0;
  unsigned long measurementStartTime = 0;
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
  state.tofRange = preferences.getString("tof_range", "short");
  
  pinTofSda = preferences.getInt("pin_tof_sda", 4);
  pinTofScl = preferences.getInt("pin_tof_scl", 5);
  pinEncSda = preferences.getInt("pin_enc_sda", 10);
  pinEncScl = preferences.getInt("pin_enc_scl", 11);
  pinHxDt = preferences.getInt("pin_hx_dt", 6);
  pinHxSck = preferences.getInt("pin_hx_sck", 7);
  preferences.end();
  
  // Validaciones de seguridad - Forzar 10ms (100Hz) si no es válido o es la primera vez
  if (sampleRateMs < 2 || sampleRateMs > 1000) sampleRateMs = 10;
  if (currentToFModel == "" || currentToFModel.length() < 3) currentToFModel = "vl53l0x";

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
  
  preferences.putInt("pin_tof_sda", pinTofSda);
  preferences.putInt("pin_tof_scl", pinTofScl);
  preferences.putInt("pin_enc_sda", pinEncSda);
  preferences.putInt("pin_enc_scl", pinEncScl);
  preferences.putInt("pin_hx_dt", pinHxDt);
  preferences.putInt("pin_hx_sck", pinHxSck);
  preferences.end();
  Serial.println("[NVS] Guardado exitoso");
}

// ═══════════════════════════════════════════════════════════════
// INICIALIZACIÓN DE SENSORES
// ═══════════════════════════════════════════════════════════════
void initSensors() {
  // Bus I2C #0 — ToF (Varios modelos)
  I2C_TOF.begin(pinTofSda, pinTofScl);
  I2C_TOF.setClock(400000);

  state.tofReady = false;
  switch (activeToF) {
    case MODEL_L0X:
      tof0X.setBus(&I2C_TOF);
      if (tof0X.init()) {
        if (state.tofRange == "long") {
            // Habilitar perfil "Long Range" para alcanzar 2 metros
            tof0X.setSignalRateLimit(0.1);
            tof0X.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
            tof0X.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);
        }

        uint32_t budget = (sampleRateMs * 1000) - 2000;
        if (state.tofRange == "long") {
            // Mínimo recomendado para long range es 33000us, pero forzaremos mínimo 20000us para estabilidad
            if (budget < 20000) budget = 20000; 
        } else {
            if (budget < 10000) budget = 10000;
        }
        
        tof0X.setMeasurementTimingBudget(budget);
        tof0X.startContinuous(sampleRateMs);
        state.tofReady = true;
        Serial.printf("[OK] VL53L0X (ToF) inicializado a %d ms (Modo %s)\n", sampleRateMs, state.tofRange == "long" ? "Long Range" : "Short Range");
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
  I2C_ENC.begin(pinEncSda, pinEncScl);
  I2C_ENC.setClock(400000);
  encoder.begin(255); 
  if (encoder.isConnected()) {
    state.encoderReady = true;
    Serial.println("[OK] AS5600 (Encoder) en Bus1 GPIO10/11");
  } else {
    Serial.println("[WARN] AS5600 no encontrado en Bus1");
  }

  // HX711 (Celda de Carga)
  loadCell.begin(pinHxDt, pinHxSck);
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

    if (!timeout) {
      // FILTRO ANTI-8190: Si el sensor pierde el objeto (>8000), ignorar la lectura.
      // Conservamos el último valor válido para evitar saltos/escalones en la gráfica.
      // La parada de seguridad en Auto usa el valor crudo (dist) más abajo.
      if (dist < 8000) {
        state.prevDistance = state.lastDistance;
        state.lastDistance = dist;
      }

      if (state.triggerEnabled && state.isWaitingForTrigger) {
        if (abs(state.lastDistance - state.initialDistance) >= state.triggerThreshold) {
          state.isWaitingForTrigger = false;
          state.measuring = true;
          state.measurementStartTime = millis();
          state.tofMeasure.active = true;
          state.encMeasure.active = true;
          state.hxMeasure.active = true;
          setLED(CRGB::Blue);
          resetBuffer();
          ws.textAll("{\"command\":\"TRIGGER_START\",\"t\":0}");
          Serial.println("[AUTO] ¡Movimiento detectado!");
        }
      } 
      else if (state.measuring) {
        float stopDist = (state.triggerEnabled && state.tubeLength > 0) ? state.tubeLength : state.distMax;
        bool shouldStop = false;
        
        if (state.triggerEnabled) {
            // Lógica de parada inteligente (Solo modo Automático)
            // 1. Si el objeto se ALEJA: detener en el largo del tubo
            if (state.initialDistance < stopDist) {
                if (state.lastDistance >= stopDist) shouldStop = true;
            } 
            // 2. Si el objeto BAJA: detener 1cm antes de la zona muerta
            else {
                if (state.lastDistance <= (state.distMin - 10)) shouldStop = true;
            }
            
            // 3. SEGURIDAD: Si el sensor manda error (perdió el objeto), detener toma
            if (!shouldStop && dist >= 8000) {
                shouldStop = true;
                Serial.println("[AUTO] Señal perdida o fuera de rango. Deteniendo.");
            }

            // 4. Límite físico
            if (!shouldStop && state.lastDistance >= state.distMax) shouldStop = true;
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

      if (dt > 0 && state.lastSampleTime > 0) {
        float newVel = (state.lastDistance - state.prevDistance) / (dt * 1000.0); 
        state.lastAccel = (newVel - state.lastVelocity) / dt; 
        state.prevVelocity = state.lastVelocity;
        state.lastVelocity = newVel;
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
      String clientIp = client->remoteIP().toString();
      if (currentTeacherIp != clientIp && currentLeaderIp != clientIp) return; // PROTEGER
      int newRate = msg.substring(9).toInt();
      if (newRate >= 2 && newRate <= 1000) {
        sampleRateMs = newRate;
        saveSettings();
        Serial.printf("[CMD] Frecuencia de muestreo cambiada a: %d ms\n", sampleRateMs);
        ws.textAll("{\"config\":{\"sample_rate\":" + String(sampleRateMs) + "}}");
      }
    }
    else if (msg.startsWith("SET_PINS:")) {
      String clientIp = client->remoteIP().toString();
      if (currentTeacherIp != clientIp) return; // SOLO DOCENTE (Líder NO puede reasignar pines de hardware)
      
      String payload = msg.substring(9);
      // Formato: tofSda,tofScl,encSda,encScl,hxDt,hxSck
      int p[6];
      int lastIndex = 0;
      for (int i=0; i<6; i++) {
        int commaIndex = payload.indexOf(',', lastIndex);
        if (commaIndex == -1 && i == 5) commaIndex = payload.length();
        if (commaIndex != -1) {
          p[i] = payload.substring(lastIndex, commaIndex).toInt();
          lastIndex = commaIndex + 1;
        } else { p[i] = -1; }
      }
      
      if (p[0] != -1) {
        pinTofSda = p[0]; pinTofScl = p[1];
        pinEncSda = p[2]; pinEncScl = p[3];
        pinHxDt = p[4]; pinHxSck = p[5];
        saveSettings();
        Serial.println("[CMD] Pines dinámicos actualizados. Reiniciando...");
        delay(500);
        ESP.restart();
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
  doc["config"]["tube_length"] = state.tubeLength;
  doc["config"]["tof_range"] = state.tofRange;

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
    doc["config"]["tube_length"] = state.tubeLength;
    doc["config"]["tof_range"] = state.tofRange;
    doc["auth"]["leader_ip"] = currentLeaderIp;
    doc["auth"]["teacher_ip"] = currentTeacherIp;
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (SystemFS.exists("/config.json")) {
      req->send(SystemFS, "/config.json", "application/json");
    } else {
      req->send(200, "application/json", "{\"lab_name\":\"Physys Lab — UMNG\",\"version\":\"v9.0\"}");
    }
  });

  server.on("/api/gpio", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    struct PinInfo { int gpio; const char* label; const char* fn; };
    PinInfo allPins[] = {
      {0, "BOOT/0", "boot"}, {1, "1", "gpio"}, {2, "2", "gpio"}, {3, "3", "gpio"}, 
      {4, "4", "gpio"}, {5, "5", "gpio"}, {6, "6", "gpio"}, {7, "7", "gpio"},
      {8, "8", "gpio"}, {9, "9", "gpio"}, {10, "10", "gpio"}, {11, "11", "gpio"},
      {12, "12", "gpio"}, {13, "13", "gpio"}, {14, "14", "gpio"}, {15, "15", "gpio"},
      {16, "16", "gpio"}, {17, "17", "gpio"}, {18, "18", "gpio"}, {21, "21", "gpio"},
      {26, "26", "gpio"}, {38, "38", "gpio"}, {39, "39", "gpio"}, {40, "40", "gpio"}, 
      {41, "41", "gpio"}, {42, "42", "gpio"}, {43, "43", "TX"}, {44, "44", "RX"}, 
      {45, "45", "gpio"}, {46, "46", "gpio"}, {47, "47", "gpio"}, {48, "48", "led"}
    };
    const int numPins = sizeof(allPins) / sizeof(allPins[0]);
    
    JsonArray arr = doc["pins"].to<JsonArray>();
    for (int i = 0; i < numPins; i++) {
      JsonObject pin = arr.add<JsonObject>();
      pin["g"] = allPins[i].gpio;
      pin["l"] = allPins[i].label;
      pin["f"] = allPins[i].fn;
      
      // Sobrescribir labels si el pin está asignado dinámicamente
      if(allPins[i].gpio == pinTofSda) { pin["l"] = "TOF_SDA"; pin["f"] = "i2c"; }
      else if(allPins[i].gpio == pinTofScl) { pin["l"] = "TOF_SCL"; pin["f"] = "i2c"; }
      else if(allPins[i].gpio == pinEncSda) { pin["l"] = "ENC_SDA"; pin["f"] = "i2c"; }
      else if(allPins[i].gpio == pinEncScl) { pin["l"] = "ENC_SCL"; pin["f"] = "i2c"; }
      else if(allPins[i].gpio == pinHxDt) { pin["l"] = "HX_DT"; pin["f"] = "serial"; }
      else if(allPins[i].gpio == pinHxSck) { pin["l"] = "HX_SCK"; pin["f"] = "serial"; }
      
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
