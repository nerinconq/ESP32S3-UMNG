#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "HX711.h"
#include <LittleFS.h>
#include <esp_mac.h>

HX711 scale;
AsyncWebServer server(80);
DNSServer dnsServer;

const int LOADCELL_DOUT_PIN = 6;
const int LOADCELL_SCK_PIN = 7;

// --- VARIABLES DE INSTRUMENTACIÓN ---
float pesoCrudo = 0.0;
float pesoFiltrado = 0.0;
const float ALPHA_LENTO = 0.15;
const float ALPHA_RAPIDO = 0.85;
const float UMBRAL_SALTO = 2.0;

// Tu factor de calibración hallado en Fase 1
float factorCalibracion = -1081.3726; 

void setup() {
  Serial.begin(115200);
  delay(2000);

  if(!LittleFS.begin(true)) return;

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(factorCalibracion);
  scale.tare(); 

  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP);
  char macStr[10];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]);
  String ssid = "PhySyS-Lab-" + String(macStr);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), "12345678");
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Endpoint Pedagógico: Envía RAW y FILTRADO
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{\"raw\":" + String(pesoCrudo, 2) + 
                  ",\"filt\":" + String(pesoFiltrado, 2) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  server.on("/tara", HTTP_GET, [](AsyncWebServerRequest *req){
    scale.tare();
    pesoFiltrado = 0.0; 
    req->send(200, "text/plain", "OK");
  });

  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.onNotFound([](AsyncWebServerRequest *req){ req->redirect("http://192.168.4.1/"); });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  if (scale.is_ready()) {
    pesoCrudo = scale.get_units(3); 
    float diferencia = abs(pesoCrudo - pesoFiltrado);
    float alphaActual = (diferencia > UMBRAL_SALTO) ? ALPHA_RAPIDO : ALPHA_LENTO;
    pesoFiltrado = (alphaActual * pesoCrudo) + ((1.0 - alphaActual) * pesoFiltrado);
    if (abs(pesoFiltrado) < 0.5) pesoFiltrado = 0.0;
  }
}
