#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include <FastLED.h>

// ═══════════════════════════════════════════════════════════════
// CONFIGURACIÓN DE PINES
// ═══════════════════════════════════════════════════════════════
#define LED_PIN          48
#define NUM_LEDS          1

CRGB leds[NUM_LEDS];
WebServer server(80);
bool cameraDetected = false;
String detectedModelName = "Ninguno";

bool initCameraWithPinout(int model) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.xclk_freq_hz = 10000000;
  config.frame_size = FRAMESIZE_SVGA;

  if (model == 0) { // Freenove (ESP32S3_EYE)
    config.pin_d0 = 11;
    config.pin_d1 = 9;
    config.pin_d2 = 8;
    config.pin_d3 = 10;
    config.pin_d4 = 12;
    config.pin_d5 = 18;
    config.pin_d6 = 17;
    config.pin_d7 = 16;
    config.pin_xclk = 15;
    config.pin_pclk = 13;
    config.pin_vsync = 6;
    config.pin_href = 7;
    config.pin_sccb_sda = 4;
    config.pin_sccb_scl = 5;
    config.pin_pwdn = -1;
    config.pin_reset = -1;
  } else { // Standard S3-CAM (ESP32S3_CAM_LCD)
    config.pin_d0 = 13;
    config.pin_d1 = 47;
    config.pin_d2 = 14;
    config.pin_d3 = 3;
    config.pin_d4 = 12;
    config.pin_d5 = 42;
    config.pin_d6 = 41;
    config.pin_d7 = 39;
    config.pin_xclk = 40;
    config.pin_pclk = 11;
    config.pin_vsync = 21;
    config.pin_href = 38;
    config.pin_sccb_sda = 17;
    config.pin_sccb_scl = 18;
    config.pin_pwdn = -1;
    config.pin_reset = -1;
  }

  esp_err_t err = esp_camera_init(&config);
  return (err == ESP_OK);
}

void handle_jpg() {
  if (!cameraDetected) {
    server.send(404, "text/plain", "Camara no detectada en el arranque.");
    return;
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Fallo al capturar frame");
    server.send(500, "text/plain", "Fallo al capturar frame");
    return;
  }
  
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  
  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);
  
  esp_camera_fb_return(fb);
}

void handle_stream() {
  if (!cameraDetected) {
    server.send(404, "text/plain", "Camara no detectada en el arranque.");
    return;
  }

  WiFiClient client = server.client();
  
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);
  
  while (true) {
    if (!client.connected()) break;
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAM] Fallo de captura en stream");
      break;
    }
    
    String chunk = "--frame\r\n";
    chunk += "Content-Type: image/jpeg\r\n";
    chunk += "Content-Length: " + String(fb->len) + "\r\n\r\n";
    client.print(chunk);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    
    esp_camera_fb_return(fb);
    delay(50); // ~20 FPS
  }
}

void handle_root() {
  String html = "<html><head><title>Physys Lab - Camera Test</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #0f0c20; color: #e2e8f0; text-align: center; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; }";
  html += "h1 { color: #38bdf8; font-size: 2.2rem; margin-bottom: 5px; text-shadow: 0 0 10px rgba(56,189,248,0.3); }";
  html += "p { color: #94a3b8; margin-top: 0; margin-bottom: 25px; }";
  html += ".card { background: rgba(30, 41, 59, 0.5); border: 1px solid rgba(255,255,255,0.1); border-radius: 16px; padding: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.5); backdrop-filter: blur(10px); max-width: 660px; width: 90%; }";
  html += ".feed-container { position: relative; width: 100%; border-radius: 12px; overflow: hidden; border: 2px solid #38bdf8; background: #000; display: flex; justify-content: center; min-height: 300px; align-items: center; }";
  html += "img { width: 100%; height: auto; display: block; object-fit: contain; }";
  html += ".btn-group { margin-top: 20px; display: flex; gap: 15px; justify-content: center; }";
  html += ".btn { background: linear-gradient(135deg, #0284c7 0%, #0369a1 100%); color: white; border: none; padding: 12px 24px; font-size: 1rem; font-weight: 600; border-radius: 8px; cursor: pointer; transition: all 0.2s ease; box-shadow: 0 4px 12px rgba(3,105,161,0.3); }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 6px 16px rgba(3,105,161,0.5); }";
  html += ".btn-secondary { background: linear-gradient(135deg, #475569 0%, #334155 100%); box-shadow: 0 4px 12px rgba(51,65,85,0.3); }";
  html += ".btn-secondary:hover { box-shadow: 0 6px 16px rgba(51,65,85,0.5); }";
  html += ".error-box { color: #f87171; padding: 20px; font-size: 1.1rem; border: 1px dashed #ef4444; border-radius: 8px; background: rgba(239, 68, 68, 0.1); text-align: left; line-height: 1.6; }";
  html += "</style></head><body>";
  html += "<h1>📷 Physys Lab - Camera Test</h1>";
  html += "<p>Hardware: ESP32-S3-CAM | OPI PSRAM | WiFi AP</p>";
  html += "<div class='card'>";
  html += "  <div class='feed-container'>";
  
  if (cameraDetected) {
    html += "    <img id='viewport' src='/stream' />";
  } else {
    html += "    <div class='error-box'>";
    html += "      <span style='font-size: 1.5rem;'>⚠️ ¡Camara No Encontrada!</span><br><br>";
    html += "      El procesador no detecto el sensor de la camara (OV2640).<br><br>";
    html += "      <strong>Posibles causas y soluciones:</strong><br>";
    html += "      1. <strong>Falta de conexion fisica:</strong> Verifica que la cinta flex de la camara este insertada hasta el fondo del conector FPC y con la pestaña negra de seguridad cerrada hacia abajo.<br>";
    html += "      2. <strong>Problema de hardware:</strong> Asegurate de que el sensor OV2640 no este dañado o tenga los lentes sucios.<br>";
    html += "      3. <strong>Pinout incompatible:</strong> Si tu placa no es Freenove ni AI-Thinker/Waveshare estandar, puede tener un mapeo de pines especial.";
    html += "    </div>";
  }
  
  html += "  </div>";
  
  if (cameraDetected) {
    html += "  <p style='margin-top: 15px; color: #10b981; font-weight: bold;'>✔ Camara detectada correctamente en modo: " + detectedModelName + "</p>";
    html += "  <div class='btn-group'>";
    html += "    <button class='btn' onclick='setView(\"/stream\")'>Ver Video en Vivo</button>";
    html += "    <button class='btn btn-secondary' onclick='setView(\"/capture\")'>Tomar Foto Estatica</button>";
    html += "  </div>";
  }
  
  html += "</div>";
  html += "<script>";
  html += "function setView(url) {";
  html += "  var img = document.getElementById('viewport');";
  html += "  if (url === '/capture') {";
  html += "    img.src = '/capture?t=' + new Date().getTime();";
  html += "  } else {";
  html += "    img.src = '/stream';";
  html += "  }";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[SYS] Inicializando Prueba de Camara con Autodeteccion...");

  // Inicializar LED RGB Onboard
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  leds[0] = CRGB::Orange; // Naranja = Inicializando
  FastLED.show();

  // Autodeteccion de placa
  Serial.println("[CAM] Intentando inicializar con pinout Freenove (S3_EYE)...");
  if (initCameraWithPinout(0)) {
    Serial.println("[CAM] ¡Exito! Camara detectada en pinout Freenove.");
    cameraDetected = true;
    detectedModelName = "Freenove (ESP32S3_EYE)";
    leds[0] = CRGB::Green; // Verde = Freenove detectada
    FastLED.show();
  } else {
    Serial.println("[CAM] Fallo. Intentando pinout Estandar S3-CAM (S3_CAM_LCD)...");
    if (initCameraWithPinout(1)) {
      Serial.println("[CAM] ¡Exito! Camara detectada en pinout Estandar S3-CAM.");
      cameraDetected = true;
      detectedModelName = "Estandar S3-CAM (ESP32S3_CAM_LCD)";
      leds[0] = CRGB::Cyan; // Cian = Placa estandar detectada
      FastLED.show();
    } else {
      Serial.println("[ERR] No se encontro ninguna camara OV2640 conectada en ningun pinout.");
      cameraDetected = false;
      leds[0] = CRGB::Red; // Rojo = No se detecto camara
      FastLED.show();
    }
  }

  // AUNQUE NO SE DETECTE LA CÁMARA, LEVANTAMOS EL WIFI PARA DIAGNÓSTICO
  Serial.println("[SYS] Levantando Punto de Acceso WiFi...");
  WiFi.softAP("Physys-CamTest", ""); // AP abierto
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[AP] Access Point iniciado: Physys-CamTest\n");
  Serial.print("[AP] Conectate y ve en el navegador a: http://");
  Serial.println(IP);

  // Configurar Endpoints
  server.on("/", HTTP_GET, handle_root);
  server.on("/capture", HTTP_GET, handle_jpg);
  server.on("/stream", HTTP_GET, handle_stream);
  server.begin();

  Serial.println("[OK] Servidor Web listo.");
}

void loop() {
  server.handleClient();
  delay(2);
}
