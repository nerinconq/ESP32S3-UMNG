#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <VL6180X.h>
#include <esp_mac.h> // Solución profesional: Lectura directa del hardware

VL6180X sensor;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// Variables globales de cinemática
unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0;
float velocidadAnterior = 0;

float dist = 0, vel = 0, acel = 0;

// === HTML 100% OFFLINE CON CANVAS NATIVO ===
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>PhySyS Lab - v2</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 10px; background-color: #f4f4f9; color: #333; }
    h2 { margin-top: 5px; color: #0056b3; font-size: 20px; }
    .dashboard { display: flex; justify-content: space-around; flex-wrap: wrap; margin-bottom: 15px; }
    .data-box { background: white; padding: 10px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); width: 30%; min-width: 100px; margin-bottom: 5px; }
    .data-box span { display: block; font-size: 22px; font-weight: bold; margin-top: 5px; }
    .val-p { color: #d9534f; }
    .val-v { color: #5cb85c; }
    .val-a { color: #f0ad4e; }
    canvas { background: white; border: 2px solid #ccc; border-radius: 8px; width: 100%; max-width: 600px; height: 250px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
    .ejes { font-size: 12px; color: #666; margin-top: 5px; }
  </style>
</head>
<body>
  <h2>PhySyS: Cinemática en Tiempo Real</h2>
  
  <div class="dashboard">
    <div class="data-box">Posición (m) <span id="valP" class="val-p">0.000</span></div>
    <div class="data-box">Velocidad (m/s) <span id="valV" class="val-v">0.000</span></div>
    <div class="data-box">Acel. (m/s²) <span id="valA" class="val-a">0.000</span></div>
  </div>
  
  <canvas id="grafica" width="600" height="250"></canvas>
  <div class="ejes">Eje X: Tiempo (s) | Eje Y: Posición (m)</div>
  
  <script>
    const canvas = document.getElementById('grafica');
    const ctx = canvas.getContext('2d');
    
    let historico = []; 
    const maxPuntos = 50;

    function dibujarGrafica() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      if(historico.length < 2) return;

      let t_inicio = historico[0].t;
      let t_fin = historico[historico.length - 1].t;
      let rangoT = t_fin - t_inicio;
      if (rangoT === 0) rangoT = 1;

      let maxD = Math.max(...historico.map(p => p.d), 0.4);
      let minD = 0;

      ctx.strokeStyle = '#eee';
      ctx.lineWidth = 1;
      ctx.beginPath();
      for(let i=1; i<5; i++) {
        let y = canvas.height * (i/5);
        ctx.moveTo(0, y); ctx.lineTo(canvas.width, y);
      }
      ctx.stroke();

      ctx.beginPath();
      ctx.strokeStyle = '#d9534f'; 
      ctx.lineWidth = 3;
      ctx.lineJoin = 'round';

      for(let i = 0; i < historico.length; i++) {
        let p = historico[i];
        let x = ((p.t - t_inicio) / rangoT) * canvas.width;
        let y = canvas.height - ((p.d - minD) / (maxD - minD)) * canvas.height;
        
        if(i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    setInterval(() => {
      fetch('/datos').then(r => r.json()).then(datos => {
        document.getElementById('valP').innerText = datos.d.toFixed(3);
        document.getElementById('valV').innerText = datos.v.toFixed(3);
        document.getElementById('valA').innerText = datos.a.toFixed(3);
        
        let tiempoSegundos = datos.t / 1000.0;
        
        historico.push({ t: tiempoSegundos, d: datos.d });
        if(historico.length > maxPuntos) {
          historico.shift();
        }
        
        dibujarGrafica();
      }).catch(err => console.log("Esperando..."));
    }, 100);
  </script>
</body>
</html>
)rawliteral";
// ========================================================

void setup() {
  Serial.begin(115200);
  delay(3000); 

  Wire.begin(4, 5);
  sensor.init();
  sensor.configureDefault();
  sensor.setScaling(2); 
  sensor.startRangeContinuous(100);

  // === GENERADOR DE RED ÚNICA (MÉTODO SEGURO DE HARDWARE) ===
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); // Lee la MAC sin encender el WiFi
  
  char macStr[18];
  // Convertimos solo los últimos 2 bytes a texto hexadecimal (ej: "A3F2")
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  
  String ssidUnico = "PhySyS-" + String(macStr);
  
  // Encendemos el WiFi de manera directa y segura
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");
  // ===========================================================

  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", index_html);
  });

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
  Serial.print("Servidor PhySyS listo. Red: ");
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