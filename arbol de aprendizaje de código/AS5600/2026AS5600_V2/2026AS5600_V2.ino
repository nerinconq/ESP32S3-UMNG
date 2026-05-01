#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <AS5600.h>
#include <esp_mac.h>

AS5600 encoder;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// Constantes exactas del main.cpp para pedagogía
#define AS5600_RAW_TO_DEGREES 0.087890625
#define DEG_TO_RAD 0.01745329251

// Variables Globales
unsigned long tiempoAnterior = 0;
float anguloTotal = 0, rawAnteriorRad = 0, velAngularAnt = 0;
float ang = 0, vel = 0, acel = 0;

// === HTML INCORPORADO (V2) ===
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>PhySyS Lab Angular - v2</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 10px; background-color: #f4f4f9; color: #333; }
    h2 { margin-top: 5px; color: #0056b3; font-size: 20px; }
    .dashboard { display: flex; justify-content: space-around; flex-wrap: wrap; margin-bottom: 15px; }
    .data-box { background: white; padding: 10px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); width: 30%; min-width: 90px; margin-bottom: 5px; }
    .data-box span { display: block; font-size: 20px; font-weight: bold; margin-top: 5px; }
    canvas { background: white; border: 2px solid #ccc; border-radius: 8px; width: 100%; max-width: 600px; height: 250px; }
    .ejes { font-size: 12px; color: #666; margin-top: 5px; }
  </style>
</head>
<body>
  <h2>Cinemática Angular V2</h2>
  <div class="dashboard">
    <div class="data-box">θ (rad) <span id="valP" style="color:#d9534f;">0.000</span></div>
    <div class="data-box">ω (rad/s) <span id="valV" style="color:#5cb85c;">0.000</span></div>
    <div class="data-box">α (rad/s²) <span id="valA" style="color:#f0ad4e;">0.000</span></div>
  </div>
  <canvas id="grafica" width="600" height="250"></canvas>
  <div class="ejes">Eje X: Tiempo (s) | Eje Y: Posición Ang. (rad)</div>
  <script>
    const canvas = document.getElementById('grafica'), ctx = canvas.getContext('2d');
    let historico = [];
    
    function dibujarGrafica() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      if(historico.length < 2) return;
      let t_inicio = historico[0].t, t_fin = historico[historico.length - 1].t, rangoT = t_fin - t_inicio || 1;
      let maxD = Math.max(...historico.map(p => p.a), 1), minD = Math.min(...historico.map(p => p.a), 0);
      let margen = (maxD - minD) * 0.1 || 0.1; maxD += margen; minD -= margen;
      
      ctx.beginPath(); ctx.strokeStyle = '#d9534f'; ctx.lineWidth = 3; ctx.lineJoin = 'round';
      for(let i = 0; i < historico.length; i++) {
        let x = ((historico[i].t - t_inicio) / rangoT) * canvas.width;
        let y = canvas.height - ((historico[i].a - minD) / (maxD - minD)) * canvas.height;
        if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    setInterval(() => {
      fetch('/datos').then(r => r.json()).then(d => {
        document.getElementById('valP').innerText = d.ang.toFixed(3);
        document.getElementById('valV').innerText = d.vel.toFixed(3);
        document.getElementById('valA').innerText = d.acel.toFixed(3);
        
        historico.push({ t: d.t / 1000.0, a: d.ang });
        if(historico.length > 50) historico.shift();
        
        dibujarGrafica();
      }).catch(e => console.log("Buscando red..."));
    }, 100);
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(2000);

  // 1. Iniciar Sensor Angular
  Wire.begin(10, 11);
  encoder.begin(255); // 255 para evitar conflictos con el pin DIR
  
  if (encoder.isConnected()) {
    float rawInitial = encoder.readAngle();
    rawAnteriorRad = (rawInitial * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;
  } else {
    Serial.println("¡ALERTA! AS5600 no detectado.");
  }

  // 2. Generador de Red Única
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); 
  char macStr[18];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  String ssidUnico = "PhySyS-Ang-" + String(macStr);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");

  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  // 3. Rutas del Servidor Web
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ 
    req->send_P(200, "text/html", index_html); 
  });
  
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{\"ang\":" + String(ang, 3) + 
                  ",\"vel\":" + String(vel, 3) + 
                  ",\"acel\":" + String(acel, 3) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  // --- TRAMPAS DEL PORTAL CAUTIVO ---
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://192.168.4.1/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://192.168.4.1/");
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("http://192.168.4.1/");
  });
  server.onNotFound([](AsyncWebServerRequest *req){ 
    req->redirect("http://192.168.4.1/"); 
  });

  server.begin();
  Serial.println("Servidor V2 Angular Activo: " + ssidUnico);
}

void loop() {
  dnsServer.processNextRequest();
  
  // Lectura y conversión paso a paso
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