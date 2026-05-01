#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <VL6180X.h>
#include <LittleFS.h>
#include <esp_mac.h> 

VL6180X sensor;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0;
float velocidadAnterior = 0;
float dist = 0, vel = 0, acel = 0;

void setup() {
  Serial.begin(115200);
  delay(3000); 

  Serial.println("\n--- INICIANDO PHYSYS V4 ---");

  // 1. MONTAR SISTEMA DE ARCHIVOS
  if(!LittleFS.begin(true)){
    Serial.println("¡Error! Fallo montando LittleFS.");
    return;
  }
  Serial.println("LittleFS montado correctamente.");

  // 2. CONFIGURACIÓN DEL SENSOR CON DIAGNÓSTICO
  Wire.begin(4, 5);
  Serial.println("Buscando sensor VL6180X...");
  sensor.init();
  
  if (sensor.readReg(0x000) == 0) {
    Serial.println("¡ALERTA! El sensor NO responde. Revisa cables o reinicia la energía física (desconecta y conecta).");
  } else {
    Serial.println("¡Sensor detectado y funcionando al 100%!");
    sensor.configureDefault();
    sensor.setScaling(2); 
    sensor.startRangeContinuous(100);
  }

  // 3. RED ÚNICA DE AULA (ARRANQUE DIRECTO POR HARDWARE)
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); 
  
  char macStr[18];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  String ssidUnico = "PhySyS-" + String(macStr);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");

  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  // 4. CONFIGURACIÓN DEL SERVIDOR WEB
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{\"d\":" + String(dist, 3) + 
                  ",\"v\":" + String(vel, 3) + 
                  ",\"a\":" + String(acel, 3) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  server.begin();
  
  Serial.println("\n--------------------------------");
  Serial.print("Servidor V4 Activo en red: ");
  Serial.println(ssidUnico);
  Serial.println("--------------------------------\n");
}

void loop() {
  dnsServer.processNextRequest();
  
  float distActual = sensor.readRangeContinuousMillimeters() / 1000.0;
  unsigned long tiempoActual = millis();
  
  float dt = (tiempoActual - tiempoAnterior) / 1000.0; 

  if (dt > 0) {
    float velActual = (distActual - distanciaAnterior) / dt;
    float acelActual = (velActual - velocidadAnterior) / dt;

    dist = distActual;
    vel = velActual;
    acel = acelActual;

    distanciaAnterior = distActual;
    velocidadAnterior = velActual;
    tiempoAnterior = tiempoActual;
  }
  delay(10);
}