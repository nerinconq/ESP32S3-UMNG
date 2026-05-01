#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <VL6180X.h>
#include <LittleFS.h> // Librería para el sistema de archivos
#include <esp_mac.h>  // Librería profesional para lectura directa de MAC

VL6180X sensor;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// Variables globales de cinemática
unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0;
float velocidadAnterior = 0;
float dist = 0, vel = 0, acel = 0;

void setup() {
  Serial.begin(115200);
  delay(3000); 

  Serial.println("\n--- INICIANDO PHYSYS V3 (LittleFS) ---");

  // 1. INICIALIZAR EL SISTEMA DE ARCHIVOS
  if(!LittleFS.begin(true)){
    Serial.println("¡Error! Ocurrió un fallo montando LittleFS.");
    return;
  }
  Serial.println("LittleFS montado correctamente.");

  // 2. CONFIGURACIÓN DEL SENSOR
  Wire.begin(4, 5);
  sensor.init();
  sensor.configureDefault();
  sensor.setScaling(2); 
  sensor.startRangeContinuous(100);

  // 3. CONFIGURACIÓN DE RED Y PORTAL CAUTIVO (MÉTODO SEGURO ESP-IDF)
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); // Lee la MAC sin encender el WiFi
  
  char macStr[18];
  // Convertimos solo los últimos 2 bytes a texto hexadecimal (ej: "A3F2")
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  
  String ssidUnico = "PhySyS-" + String(macStr);
  
  // Encendemos el WiFi de manera directa y segura
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");

  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  // 4. RUTAS DEL SERVIDOR WEB
  // Esta sola línea le dice al ESP32 que busque archivos en LittleFS
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
  Serial.println("Servidor Web Activo.");
  Serial.print("Red creada con éxito: ");
  Serial.println(ssidUnico);
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