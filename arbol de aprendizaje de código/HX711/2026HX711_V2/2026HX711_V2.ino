#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "HX711.h"
#include <esp_mac.h>

HX711 scale;
AsyncWebServer server(80);
DNSServer dnsServer;

// --- VARIABLES DEL FILTRO ADAPTATIVO ---
float fuerzaActual = 0;
float pesoFiltrado = 0.0;
const float ALPHA_LENTO = 0.15;
const float ALPHA_RAPIDO = 0.85;
const float UMBRAL_SALTO = 2.0;

// ¡IMPORTANTE! Reemplaza el 420.0 por el factor que hallaste en tu Fase 1
float factorCalibracion = -1081.3726; 

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8"><title>PhySyS Fuerza V2</title>
  <style>
    body { font-family: sans-serif; text-align: center; background: #f4f4f9; padding: 20px; }
    h2 { color: #333; }
    .val { font-size: 45px; font-weight: bold; color: #0056b3; margin-bottom: 20px; }
    canvas { background: white; border: 2px solid #ccc; border-radius: 8px; width: 100%; max-width: 600px; height: 300px; }
  </style>
</head>
<body>
  <h2>Dinámica: Fuerza vs Tiempo</h2>
  <div class="val"><span id="f">0.000</span> N</div>
  <canvas id="grafica" width="600" height="300"></canvas>
  <script>
    let hist = [];
    setInterval(() => {
      fetch('/datos').then(r => r.json()).then(d => {
        document.getElementById('f').innerText = d.f.toFixed(4);
        hist.push({t: Date.now(), f: d.f});
        if(hist.length > 50) hist.shift();
        dibujar();
      });
    }, 100);

    function dibujar() {
      const c = document.getElementById('grafica'), ctx = c.getContext('2d');
      ctx.clearRect(0,0,600,300);
      if(hist.length < 2) return;

      // Auto-escalado dinámico (Mejora para ver fuerzas pequeñas)
      let maxY = Math.max(...hist.map(p => p.f));
      let minY = Math.min(...hist.map(p => p.f));
      let margen = (maxY - minY) * 0.1 || 0.05; 
      maxY += margen; minY -= margen;

      ctx.beginPath(); ctx.strokeStyle='#0056b3'; ctx.lineWidth=3; ctx.lineJoin='round';
      hist.forEach((p, i) => {
        let x = (i / (hist.length - 1)) * 600;
        let y = 300 - ((p.f - minY) / (maxY - minY)) * 300;
        if(i==0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
      });
      ctx.stroke();
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(2000);

  // Iniciar Celda
  scale.begin(6, 7);
  scale.set_scale(factorCalibracion);
  scale.tare(); // Pone a cero al arrancar

  // Red WiFi
  uint8_t baseMac[6]; esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP);
  String ssid = "PhySyS-Fuerza-" + String(baseMac[5], HEX);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), "12345678");
  dnsServer.start(53, "*", WiFi.softAPIP());

  // Servidor Web
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/html", index_html); });
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *r){
    r->send(200, "application/json", "{\"f\":" + String(fuerzaActual, 4) + "}");
  });
  
  // Trampas Portal Cautivo
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
  server.onNotFound([](AsyncWebServerRequest *req){ req->redirect("http://192.168.4.1/"); });

  server.begin();
  Serial.println("Fase 2 Lista. Red: " + ssid);
}

void loop() {
  dnsServer.processNextRequest();
  
  if (scale.is_ready()) {
    // 1. Lectura del sensor
    float pesoCrudo = scale.get_units(3); 

    // 2. Lógica del Filtro Adaptativo
    float diferencia = abs(pesoCrudo - pesoFiltrado);
    float alphaActual = (diferencia > UMBRAL_SALTO) ? ALPHA_RAPIDO : ALPHA_LENTO;

    // Aplicar EMA
    pesoFiltrado = (alphaActual * pesoCrudo) + ((1.0 - alphaActual) * pesoFiltrado);

    // Banda Muerta
    if (abs(pesoFiltrado) < 0.5) pesoFiltrado = 0.0;

    // 3. Conversión a Fuerza (Newtons)
    fuerzaActual = (pesoFiltrado / 1000.0) * 9.80665;
  }
}