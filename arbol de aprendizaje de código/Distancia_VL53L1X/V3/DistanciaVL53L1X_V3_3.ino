#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <VL53L1X.h>
#include <LittleFS.h>
#include <esp_mac.h> 

VL53L1X sensor;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// Variables de cinemática
unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0;
float velocidadAnterior = 0;
float dist = 0, vel = 0, acel = 0;

void setup() {
  Serial.begin(115200);
  delay(3000); 

  Serial.println("\n--- INICIANDO PHYSYS V3 (VL53L1X + LittleFS) ---");

  // 1. MONTAR SISTEMA DE ARCHIVOS
  if(!LittleFS.begin(true)){
    Serial.println("¡Error! Fallo montando LittleFS.");
    return;
  }
  Serial.println("LittleFS montado correctamente.");

  // 2. CONFIGURACIÓN DEL SENSOR VL53L1X CON DIAGNÓSTICO
  Wire.begin(4, 5);
  Serial.println("Buscando sensor VL53L1X...");
  
  if (!sensor.init()) {
    Serial.println("¡ALERTA! El sensor VL53L1X NO responde. Revisa cables (SDA:4, SCL:5) o la alimentación.");
  } else {
    Serial.println("¡Sensor VL53L1X detectado y funcionando!");
    sensor.setDistanceMode(VL53L1X::Long); // Modo largo hasta 4 metros
    sensor.setMeasurementTimingBudget(50000);
    sensor.startContinuous(50);
  }

  // 3. IDENTIFICACIÓN ÚNICA (Red por Hardware)
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); 
  
  char macStr[5];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  String ssidUnico = "PhySyS-" + String(macStr);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");

  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  // 4. CONFIGURACIÓN DEL SERVIDOR WEB (Lee de LittleFS)
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
  Serial.print("Servidor V3 Activo en red: ");
  Serial.println(ssidUnico);
  Serial.println("--------------------------------\n");
}

void loop() {
  dnsServer.processNextRequest();
  
  // Lectura en mm convertida a metros
  float distActual = sensor.read() / 1000.0;
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
}