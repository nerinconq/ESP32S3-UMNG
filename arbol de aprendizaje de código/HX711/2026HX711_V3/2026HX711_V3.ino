#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "HX711.h"
#include <LittleFS.h>
#include <esp_mac.h>

HX711 scale;
AsyncWebServer server(80);
DNSServer dnsServer;

// --- CONFIGURACIÓN DE PINES ---
const int LOADCELL_DOUT_PIN = 6;
const int LOADCELL_SCK_PIN = 7;

// --- VARIABLES DE DATOS Y FILTRO ADAPTATIVO ---
float pesoCrudo = 0.0;
float pesoFiltrado = 0.0;
const float ALPHA_LENTO = 0.15;
const float ALPHA_RAPIDO = 0.85;
const float UMBRAL_SALTO = 2.0;

// Factor exacto de tu celda de 1kg
float factorCalibracion = -1081.3726; 

void setup() {
  Serial.begin(115200);
  delay(3000);

  // 1. Iniciar LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("Error montando LittleFS");
    return;
  }

  // 2. Iniciar Celda de Carga
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(factorCalibracion);
  scale.tare(); // Tara inicial al encender

  // 3. Red Única PhySyS
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP);
  char macStr[10];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]);
  String ssid = "PhySyS-Fza-" + String(macStr);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), "12345678");
  dnsServer.start(53, "*", WiFi.softAPIP());

  // --- RUTAS DEL SERVIDOR ---
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    // Enviamos el dato ya filtrado al celular
    String json = "{\"g\":" + String(pesoFiltrado, 2) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  // La función de TARA remota
  server.on("/tara", HTTP_GET, [](AsyncWebServerRequest *req){
    scale.tare();
    pesoFiltrado = 0.0; // Poner a cero el filtro internamente
    req->send(200, "text/plain", "Tarado OK");
  });

  // Trampas de Portal Cautivo
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.onNotFound([](AsyncWebServerRequest *req){ req->redirect("http://192.168.4.1/"); });

  server.begin();
  Serial.println("Servidor Fase 3 PRO Activo.");
}

void loop() {
  dnsServer.processNextRequest();
  
  if (scale.is_ready()) {
    // 1. Lectura del sensor
    pesoCrudo = scale.get_units(3); 

    // 2. Lógica del Filtro Adaptativo (La Caja de Cambios)
    float diferencia = abs(pesoCrudo - pesoFiltrado);
    float alphaActual = (diferencia > UMBRAL_SALTO) ? ALPHA_RAPIDO : ALPHA_LENTO;

    // Aplicar Fórmula EMA
    pesoFiltrado = (alphaActual * pesoCrudo) + ((1.0 - alphaActual) * pesoFiltrado);

    // Banda Muerta (Evitar que muestre -0.2g o 0.3g por la mesa)
    if (abs(pesoFiltrado) < 0.5) {
      pesoFiltrado = 0.0;
    }
  }
}