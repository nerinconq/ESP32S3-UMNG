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
unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0, velocidadAnterior = 0;
float dist = 0, vel = 0, acel = 0;

void setup() {
  Serial.begin(115200);
  delay(3000); 

  if(!LittleFS.begin(true)){
    Serial.println("Error montando LittleFS.");
    return;
  }

  Wire.begin(4, 5); // SDA, SCL
  sensor.setTimeout(500); 
  
  if (!sensor.init()) {
    Serial.println("¡Error de Hardware! VL53L1X no detectado.");
  } else {
    Serial.println("VL53L1X Listo.");
    sensor.setDistanceMode(VL53L1X::Long);
    sensor.setMeasurementTimingBudget(50000);
    sensor.startContinuous(50);
  }

  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); 
  char macStr[5];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  String ssidUnico = "PhySyS-" + String(macStr);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{\"d\":" + String(dist, 3) + 
                  ",\"v\":" + String(vel, 3) + 
                  ",\"a\":" + String(acel, 3) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  server.onNotFound([](AsyncWebServerRequest *request){ request->redirect("/"); });
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  
  unsigned long tiempoActual = millis();
  float dt = (tiempoActual - tiempoAnterior) / 1000.0; 

  // SOLO calculamos si han pasado al menos 50ms para evitar dt = 0
  if (dt >= 0.050) { 
    
    // Lectura bloqueante con timeout para asegurar un dato
    float distActual = sensor.read() / 1000.0; 
    
    // Filtro: Si el sensor da timeout, arroja 0 o 65.5. Los ignoramos.
    if (!sensor.timeoutOccurred() && distActual > 0.0 && distActual < 5.0) {
      
      float velActual = (distActual - distanciaAnterior) / dt;
      float acelActual = (velActual - velocidadAnterior) / dt;
      
      dist = distActual; 
      vel = velActual; 
      acel = acelActual;
      
      distanciaAnterior = distActual; 
      velocidadAnterior = velActual;
    }
    
    // El tiempo siempre avanza, incluso si el sensor falla
    tiempoAnterior = tiempoActual;
    
    // Impresión Serial para que verifiques en el PC qué está leyendo físicamente el sensor
    Serial.print("D:"); Serial.print(dist);
    Serial.print(" V:"); Serial.print(vel);
    Serial.print(" A:"); Serial.println(acel);
  }
}