#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <AS5600.h>
#include <LittleFS.h>
#include <esp_mac.h>

AS5600 encoder;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// --- CONFIGURACIÓN DE PINES Y CONSTANTES ---
#define ANG_SDA 10
#define ANG_SCL 11
#define ANG_DIR 16
#define ANG_GP0 17

#define AS5600_RAW_TO_DEGREES 0.087890625
#define DEG_TO_RAD 0.01745329251

// Variables de cinemática
unsigned long tiempoAnterior = 0;
float anguloTotal = 0, rawAnteriorRad = 0, velAngularAnt = 0;
float ang = 0, vel = 0, acel = 0;
bool estadoInvertido = false;

void setup() {
  Serial.begin(115200);
  delay(3000);

  // Configuración de Pines de Control
  pinMode(ANG_DIR, OUTPUT);
  pinMode(ANG_GP0, INPUT);
  digitalWrite(ANG_DIR, LOW); // Sentido normal inicial

  if(!LittleFS.begin(true)) return;

  Wire.begin(ANG_SDA, ANG_SCL);
  encoder.begin(255); // Nosotros controlamos DIR manualmente

  if (encoder.isConnected()) {
    float rawInitial = encoder.readAngle();
    rawAnteriorRad = (rawInitial * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;
  }

  // Red Única con MAC
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP);
  char macStr[18];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]);
  String ssidUnico = "PhySyS-Ang-" + String(macStr);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // --- RUTAS DEL SERVIDOR WEB ---
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    int gpoStatus = digitalRead(ANG_GP0);
    String json = "{\"ang\":" + String(ang, 3) + 
                  ",\"vel\":" + String(vel, 3) + 
                  ",\"acel\":" + String(acel, 3) + 
                  ",\"gpo\":" + String(gpoStatus) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  // La "Magia" del 1/0 a distancia
  server.on("/toggleDIR", HTTP_GET, [](AsyncWebServerRequest *req){
    estadoInvertido = !estadoInvertido;
    digitalWrite(ANG_DIR, estadoInvertido ? HIGH : LOW);
    
    // Reiniciamos referencia para evitar salto en la gráfica
    float rawAngle = encoder.readAngle();
    rawAnteriorRad = (rawAngle * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;
    
    req->send(200, "text/plain", estadoInvertido ? "1" : "0");
  });

  // Trampas de Portal Cautivo
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.onNotFound([](AsyncWebServerRequest *req){ req->redirect("http://192.168.4.1/"); });

  server.begin();
  Serial.println("PhySyS V4 Angular Activo: " + ssidUnico);
}

void loop() {
  dnsServer.processNextRequest();
  
  float rawAngle = encoder.readAngle();
  float actualRad = (rawAngle * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;
  
  float deltaAng = actualRad - rawAnteriorRad;
  if (deltaAng > PI) deltaAng -= 2.0 * PI;
  if (deltaAng < -PI) deltaAng += 2.0 * PI;
  
  anguloTotal += deltaAng;
  rawAnteriorRad = actualRad;

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