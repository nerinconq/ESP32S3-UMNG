#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <AS5600.h>
#include <LittleFS.h>  // ¡La nueva librería estrella!
#include <esp_mac.h>

AS5600 encoder;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

#define AS5600_RAW_TO_DEGREES 0.087890625
#define DEG_TO_RAD 0.01745329251

// Variables Globales
unsigned long tiempoAnterior = 0;
float anguloTotal = 0, rawAnteriorRad = 0, velAngularAnt = 0;
float ang = 0, vel = 0, acel = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n--- PHYSYS ANGULAR V3 (LITTLEFS) ---");

  // 1. INICIAR EL DISCO DURO INTERNO (LittleFS)
  if(!LittleFS.begin(true)){
    Serial.println("¡Error fatal! Fallo montando LittleFS.");
    return;
  }
  Serial.println("LittleFS montado correctamente.");

  // 2. Iniciar Sensor Angular
  Wire.begin(10, 11);
  encoder.begin(255); 
  
  if (encoder.isConnected()) {
    float rawInitial = encoder.readAngle();
    rawAnteriorRad = (rawInitial * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;
    Serial.println("Sensor AS5600 detectado.");
  } else {
    Serial.println("¡ALERTA! AS5600 no detectado en pines 10/11.");
  }

  // 3. Generador de Red Única
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); 
  char macStr[18];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  String ssidUnico = "PhySyS-Ang-" + String(macStr);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");

  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  // =========================================================
  // 4. RUTAS DEL SERVIDOR WEB
  // ¡La magia de LittleFS! Una sola línea sirve toda la web
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  // =========================================================
  
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{\"ang\":" + String(ang, 3) + 
                  ",\"vel\":" + String(vel, 3) + 
                  ",\"acel\":" + String(acel, 3) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  // Trampas del Portal Cautivo
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.onNotFound([](AsyncWebServerRequest *req){ req->redirect("http://192.168.4.1/"); });

  server.begin();
  Serial.println("Servidor V3 Activo. Red: " + ssidUnico);
}

void loop() {
  dnsServer.processNextRequest();
  
  // Lectura y conversión paso a paso (Pedagogía)
  float rawAngle = encoder.readAngle();
  float actualRad = (rawAngle * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;
  
  // Algoritmo Unwrap
  float deltaAng = actualRad - rawAnteriorRad;
  if (deltaAng > PI) deltaAng -= 2.0 * PI;
  if (deltaAng < -PI) deltaAng += 2.0 * PI;
  
  anguloTotal += deltaAng;
  rawAnteriorRad = actualRad;

  // Cinemática
  unsigned long tiempoActual = millis();
  float dt = (tiempoActual - tiempoAnterior) / 1000.0; 

  if (dt > 0) {
    vel = deltaAng / dt;
    acel = (vel - velAngularAnt) / dt;
    ang = anguloTotal;

    velAngularAnt = vel;
    tiempoAnterior = tiempoActual;
  }
  delay(10);
}