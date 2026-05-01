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
#include <AS5600.h>
#include <HX711.h>
#include <FastLED.h>

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

VL53L0X tofSensor;
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
  float lastAngleDeg = 0;       // grados
  float lastAngleRad = 0;       // radianes
  float angularVelRad = 0;      // rad/s
  float angularAccelRad = 0;    // rad/s²
  float prevAngleRad = 0;
  float prevAngularVel = 0;
  // Dinámica (HX711)
  float lastWeight = 0;         // gramos
  unsigned long lastSampleTime = 0;
} state;

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
// INICIALIZACIÓN DE SENSORES
// ═══════════════════════════════════════════════════════════════
void initSensors() {
  // Bus I2C #0 — ToF VL53L0X
  I2C_TOF.begin(TOF_SDA, TOF_SCL);
  I2C_TOF.setClock(400000);

  // VL53L0X (ToF — Cinemática) en Bus 0
  tofSensor.setBus(&I2C_TOF);
  tofSensor.setTimeout(500);
  if (tofSensor.init()) {
    tofSensor.startContinuous(50);
    state.tofReady = true;
    Serial.println("[OK] VL53L0X (ToF) en Bus0 GPIO4/5 → 0x29");
  } else {
    Serial.println("[WARN] VL53L0X no encontrado en Bus0");
  }

  // AS5600 (Encoder Magnético) en Bus 1
  I2C_ENC.begin(ENC_SDA, ENC_SCL);
  I2C_ENC.setClock(400000);
  encoder.begin(255); // Control manual de DIR
  if (encoder.isConnected()) {
    state.encoderReady = true;
    Serial.println("[OK] AS5600 (Encoder) en Bus1 GPIO10/11 → 0x36");
  } else {
    Serial.println("[WARN] AS5600 no encontrado en Bus1");
  }

  // HX711 (Celda de Carga — Fuerza/Peso)
  loadCell.begin(HX711_DT, HX711_SCK);
  if (loadCell.is_ready()) {
    loadCell.set_scale(420.0);
    loadCell.tare();
    state.loadCellReady = true;
    Serial.println("[OK] HX711 (Celda de Carga) GPIO6/7");
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
  if (dt < 0.02) return; // Mínimo 20ms entre lecturas (50Hz)

  // ToF → Posición → Velocidad → Aceleración
  if (state.tofReady) {
    float dist = tofSensor.readRangeContinuousMillimeters();
    if (!tofSensor.timeoutOccurred() && dist < 8000) {
      state.prevDistance = state.lastDistance;
      state.lastDistance = dist;

      if (dt > 0 && state.lastSampleTime > 0) {
        float newVel = (state.lastDistance - state.prevDistance) / (dt * 1000.0); // m/s
        state.lastAccel = (newVel - state.lastVelocity) / dt; // m/s²
        state.prevVelocity = state.lastVelocity;
        state.lastVelocity = newVel;
      }
    }
  }

  // Encoder → Ángulo (deg + rad) → Velocidad Angular → Aceleración Angular
  if (state.encoderReady) {
    float rawAngle = encoder.readAngle();
    state.lastAngleDeg = rawAngle * AS5600_RAW_TO_DEGREES;
    state.lastAngleRad = state.lastAngleDeg * DEG_TO_RAD;

    if (dt > 0 && state.lastSampleTime > 0) {
      float newAngVel = (state.lastAngleRad - state.prevAngleRad) / dt; // rad/s
      state.angularAccelRad = (newAngVel - state.prevAngularVel) / dt;  // rad/s²
      state.prevAngularVel = state.angularVelRad;
      state.angularVelRad = newAngVel;
    }
    state.prevAngleRad = state.lastAngleRad;
  }

  // Celda de Carga → Peso/Fuerza
  if (state.loadCellReady && loadCell.is_ready()) {
    state.lastWeight = loadCell.get_units(3); // Promedio de 3 lecturas
  }

  state.lastSampleTime = now;
}

// ═══════════════════════════════════════════════════════════════
// WEBSOCKET — STREAMING EN TIEMPO REAL
// ═══════════════════════════════════════════════════════════════
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Cliente #%u conectado\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Cliente #%u desconectado\n", client->id());
  } else if (type == WS_EVT_DATA) {
    String msg = String((char*)data).substring(0, len);
    // Comandos globales (compatibilidad)
    if (msg == "START") {
      state.measuring = true;
      state.tofMeasure.active = true;
      state.encMeasure.active = true;
      state.hxMeasure.active = true;
      setLED(CRGB::Blue);
      Serial.println("[CMD] Medición global iniciada");
    } else if (msg == "STOP") {
      state.measuring = false;
      state.tofMeasure.active = false;
      state.encMeasure.active = false;
      state.hxMeasure.active = false;
      setLED(CRGB::White);
      blinkLED(CRGB::Green, 3, 150);
      setLED(CRGB::White);
      Serial.println("[CMD] Medición global detenida");
    }
    // Comandos por sensor
    else if (msg == "START_TOF") { state.tofMeasure.active = true; state.measuring = true; setLED(CRGB::Blue); }
    else if (msg == "STOP_TOF")  { state.tofMeasure.active = false; checkGlobalStop(); }
    else if (msg == "START_ENC") { state.encMeasure.active = true; state.measuring = true; setLED(CRGB::Blue); }
    else if (msg == "STOP_ENC")  { state.encMeasure.active = false; checkGlobalStop(); }
    else if (msg == "START_HX")  { state.hxMeasure.active = true; state.measuring = true; setLED(CRGB::Blue); }
    else if (msg == "STOP_HX")   { state.hxMeasure.active = false; checkGlobalStop(); }
    else if (msg == "TARE") {
      if (state.loadCellReady) {
        loadCell.tare();
        Serial.println("[CMD] Celda de carga tarada");
      }
    }
  }
}

void checkGlobalStop() {
  if (!state.tofMeasure.active && !state.encMeasure.active && !state.hxMeasure.active) {
    state.measuring = false;
    setLED(CRGB::White);
    blinkLED(CRGB::Green, 3, 150);
    setLED(CRGB::White);
  }
}

void broadcastSensorData() {
  if (ws.count() == 0 || !state.measuring) return;

  JsonDocument doc;
  doc["t"] = millis();
  // Cinemática Lineal
  doc["dist"] = state.lastDistance;            // mm
  doc["vel"] = state.lastVelocity;             // m/s
  doc["acc"] = state.lastAccel;                // m/s²
  // Cinemática Angular
  doc["angleDeg"] = state.lastAngleDeg;        // grados
  doc["angleRad"] = state.lastAngleRad;        // radianes
  doc["angVel"] = state.angularVelRad;         // rad/s
  doc["angAcc"] = state.angularAccelRad;       // rad/s²
  // Dinámica
  doc["weight"] = state.lastWeight;            // gramos
  doc["mass"] = state.lastWeight / 1000.0;     // kg
  doc["weightN"] = (state.lastWeight / 1000.0) * 9.81; // Peso vertical (N)
  // Estado de sensores
  doc["sensors"]["tof"] = state.tofReady;
  doc["sensors"]["encoder"] = state.encoderReady;
  doc["sensors"]["loadcell"] = state.loadCellReady;

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// ═══════════════════════════════════════════════════════════════
// API ENDPOINTS
// ═══════════════════════════════════════════════════════════════
void setupAPI() {
  // Estado del sistema
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["heap"] = ESP.getFreeHeap();
    doc["psram"] = ESP.getFreePsram();
    doc["uptime"] = millis() / 1000;
    doc["sensors"]["tof"] = state.tofReady;
    doc["sensors"]["encoder"] = state.encoderReady;
    doc["sensors"]["loadcell"] = state.loadCellReady;
    doc["measuring"] = state.measuring;
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  // Configuración/branding del dispositivo (H6)
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (SystemFS.exists("/config.json")) {
      req->send(SystemFS, "/config.json", "application/json");
    } else {
      req->send(200, "application/json", "{\"lab_name\":\"Physys Lab\",\"version\":\"v6.1\"}");
    }
  });

  // Estado de pines GPIO — GPIO Viewer Embebido (H4)
  server.on("/api/gpio", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    // Todos los GPIO accesibles del ESP32-S3 WROOM-1
    // GPIO 22-34 son internos (flash/PSRAM), no expuestos
    struct PinInfo { int gpio; const char* label; const char* fn; };
    PinInfo allPins[] = {
      {0,  "BOOT",     "boot"},
      {1,  "GPIO1",    "gpio"},
      {2,  "GPIO2",    "gpio"},
      {3,  "GPIO3",    "gpio"},
      {4,  "TOF_SDA",  "i2c"},
      {5,  "TOF_SCL",  "i2c"},
      {6,  "HX_DT",    "serial"},
      {7,  "HX_SCK",   "serial"},
      {8,  "GPIO8",    "gpio"},
      {9,  "GPIO9",    "gpio"},
      {10, "ENC_SDA",  "i2c"},
      {11, "ENC_SCL",  "i2c"},
      {12, "GPIO12",   "gpio"},
      {13, "GPIO13",   "gpio"},
      {14, "GPIO14",   "gpio"},
      {15, "GPIO15",   "gpio"},
      {16, "GPIO16",   "gpio"},
      {17, "GPIO17",   "gpio"},
      {18, "GPIO18",   "gpio"},
      {19, "USB_D-",   "usb"},
      {20, "USB_D+",   "usb"},
      {21, "GPIO21",   "gpio"},
      {35, "GPIO35",   "gpio"},
      {36, "GPIO36",   "gpio"},
      {37, "GPIO37",   "gpio"},
      {38, "GPIO38",   "gpio"},
      {39, "GPIO39",   "gpio"},
      {40, "GPIO40",   "gpio"},
      {41, "GPIO41",   "gpio"},
      {42, "GPIO42",   "gpio"},
      {43, "TX",       "uart"},
      {44, "RX",       "uart"},
      {45, "GPIO45",   "gpio"},
      {46, "GPIO46",   "gpio"},
      {47, "GPIO47",   "gpio"},
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

  // Guardar experimento en partición de Datos (4MB)
  server.on("/api/data", HTTP_POST, [](AsyncWebServerRequest *req) {},
    NULL, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      String body = String((char*)data).substring(0, len);
      // Generar nombre de archivo con timestamp
      String filename = "/exp_" + String(millis()) + ".json";
      File f = DataFS.open(filename, "w");
      if (f) {
        f.print(body);
        f.close();
        req->send(200, "application/json", "{\"ok\":true,\"file\":\"" + filename + "\"}");
        blinkLED(CRGB::Green, 3, 150);
        Serial.printf("[DATA] Experimento guardado: %s (%d bytes)\n", filename.c_str(), len);
      } else {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"No se pudo escribir\"}");
        setLED(CRGB::Red);
      }
  });

  // Exportar todos los experimentos (para bitácora-UMNG)
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

  // Listar archivos de datos con tamaños
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

  // Limpiar todos los datos del ESP32
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
    setLED(CRGB::White);
    Serial.printf("[DATA] Limpieza: %d archivos eliminados\n", count);
  });

  // Leer script Python actual (GET)
  server.on("/api/python", HTTP_GET, [](AsyncWebServerRequest *req) {
    File f = SystemFS.open("/sensor_logic.py", "r");
    if (f) {
      req->send(SystemFS, "/sensor_logic.py", "text/plain");
    } else {
      req->send(404, "text/plain", "# No hay script guardado aun");
    }
  });

  // Recibir script Python del estudiante (POST)
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
        setLED(CRGB(255, 0, 255)); // Magenta
      }
  });

  // Almacenamiento disponible
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
// FACTORY RESET (Botón BOOT presionado 10s)
// ═══════════════════════════════════════════════════════════════
unsigned long resetPressStart = 0;

void checkFactoryReset() {
  if (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (resetPressStart == 0) {
      resetPressStart = millis();
    } else if (millis() - resetPressStart > 10000) {
      Serial.println("[RESET] Factory Reset activado — Limpiando datos...");
      setLED(CRGB::Red);
      // Borrar toda la partición de datos
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
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║   Physys Measurement System v1.0         ║");
  Serial.println("║   ESP32-S3 WROOM-1 | N16R8 | QIO/QSPI   ║");
  Serial.println("╚══════════════════════════════════════════╝");

  // LED de estado
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(30);
  setLED(CRGB::Yellow); // Amarillo = Inicializando

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

  if (DataFS.begin(true, "/data", 10, "userdata")) {
    Serial.printf("[FS] Datos (Experimentos): %d KB usados / %d KB total\n",
                  DataFS.usedBytes() / 1024, DataFS.totalBytes() / 1024);
  } else {
    Serial.println("[FS] ERROR: No se pudo montar partición DATOS");
  }

  // WiFi AP
  Serial.println("\n[WiFi] Configurando AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Physys-Lab");
  delay(100);
  Serial.printf("[WiFi] AP 'Physys-Lab' activo en %s\n",
                WiFi.softAPIP().toString().c_str());

  // Portal Cautivo — DNS wildcard
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.println("[DNS] Portal cautivo activo");

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
    req->redirect("http://192.168.4.1/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://192.168.4.1/");
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://192.168.4.1/");
  });

  // Archivos estáticos (CSS, JS, fuentes)
  server.serveStatic("/", SystemFS, "/www/").setDefaultFile("index.html");

  server.begin();
  Serial.println("\n[HTTP] Servidor activo en puerto 80");

  // Listo
  setLED(CRGB::White); // Blanco = Sistema listo
  Serial.println("\n✓ Physys Lab listo. Conecta a WiFi 'Physys-Lab' → http://192.168.4.1");
}

// ═══════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════
unsigned long lastBroadcast = 0;

void loop() {
  dnsServer.processNextRequest();
  ws.cleanupClients();
  checkFactoryReset();

  readSensors();

  // Broadcast datos cada 50ms (20Hz)
  if (millis() - lastBroadcast > 50) {
    broadcastSensorData();
    lastBroadcast = millis();
  }
}
