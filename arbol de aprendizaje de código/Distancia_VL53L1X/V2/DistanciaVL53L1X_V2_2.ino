#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <VL53L1X.h>
#include <esp_mac.h> // Librería para obtener la MAC por hardware

VL53L1X sensor;
AsyncWebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// Variables globales de cinemática
unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0;
float velocidadAnterior = 0;
float dist = 0, vel = 0, acel = 0;

// === HTML EMBEBIDO (Versión v2 para VL53L1X) ===
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
    canvas { background: white; border: 2px solid #ccc; border-radius: 8px; width: 100%; max-width: 600px; height: 250px; }
  </style>
</head>
<body>
  <h2>PhySyS: Cinemática (V2)</h2>
  <div class="dashboard">
    <div class="data-box">P (m) <span id="valP" style="color:#d9534f;">0.000</span></div>
    <div class="data-box">V (m/s) <span id="valV" style="color:#5cb85c;">0.000</span></div>
    <div class="data-box">A (m/s²) <span id="valA" style="color:#f0ad4e;">0.000</span></div>
  </div>
  <canvas id="grafica" width="600" height="250"></canvas>
  <script>
    const canvas = document.getElementById('grafica');
    const ctx = canvas.getContext('2d');
    let historico = [];
    
    function dibujar() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      if(historico.length < 2) return;
      
      // Escala ajustada para el rango del VL53L1X (hasta 4 metros)
      let maxD = Math.max(...historico.map(p => p.d), 4.0);
      
      ctx.beginPath();
      ctx.strokeStyle = '#d9534f';
      ctx.lineWidth = 3;
      for(let i=0; i<historico.length; i++){
        let x = (i/(historico.length-1)) * canvas.width;
        let y = canvas.height - (historico[i].d / maxD) * canvas.height;
        if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
      }
      ctx.stroke();
    }
    
    setInterval(() => {
      fetch('/datos').then(r => r.json()).then(datos => {
        document.getElementById('valP').innerText = datos.d.toFixed(3);
        document.getElementById('valV').innerText = datos.v.toFixed(3);
        document.getElementById('valA').innerText = datos.a.toFixed(3);
        historico.push({d: datos.d});
        if(historico.length > 50) historico.shift();
        dibujar();
      });
    }, 100);
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(3000); 

  Serial.println("\n--- INICIANDO PHYSYS V2 (VL53L1X) ---");

  // 1. CONFIGURACIÓN DEL SENSOR CON DIAGNÓSTICO
  Wire.begin(4, 5);
  Serial.println("Buscando sensor VL53L1X...");
  
  if (!sensor.init()) {
    Serial.println("¡ALERTA! El sensor VL53L1X NO responde. Revisa cables (SDA:4, SCL:5) o la energía física.");
  } else {
    Serial.println("¡Sensor detectado y funcionando al 100%!");
    // Configuración específica para VL53L1X
    sensor.setDistanceMode(VL53L1X::Long); 
    sensor.setMeasurementTimingBudget(50000);
    sensor.startContinuous(50);
  }

  // 2. IDENTIFICACIÓN ÚNICA (Evita colisiones en el aula)
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP); 
  
  char macStr[5];
  sprintf(macStr, "%02X%02X", baseMac[4], baseMac[5]); 
  String ssidUnico = "PhySyS-" + String(macStr);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidUnico.c_str(), "12345678");
  
  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  // 3. RUTAS DEL SERVIDOR
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", index_html);
  });

  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{\"d\":" + String(dist, 3) + 
                  ",\"v\":" + String(vel, 3) + 
                  ",\"a\":" + String(acel, 3) + 
                  ",\"t\":" + String(millis()) + "}";
    req->send(200, "application/json", json);
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.begin();
  
  Serial.println("\n--------------------------------");
  Serial.print("Servidor V2 Activo en red: ");
  Serial.println(ssidUnico);
  Serial.println("--------------------------------\n");
}

void loop() {
  dnsServer.processNextRequest();
  
  // Lectura del sensor VL53L1X en mm, dividida para tener metros
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