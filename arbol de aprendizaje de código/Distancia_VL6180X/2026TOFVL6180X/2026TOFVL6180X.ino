#include <Wire.h>
#include <VL6180X.h> // Cambiado a la librería del VL6180X

VL6180X sensor;
unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0;
float velocidadAnterior = 0;

void setup() {
  Serial.begin(115200);
  
  // Le damos 2 segundos al puerto USB por software para que se estabilice
  delay(2000); 
  
  Serial.println("\n--- INICIANDO ESP32 ---");
  Serial.println("Paso 1: USB Nativo funcionando correctamente.");

  Wire.begin(4, 5); // SDA = 4, SCL = 5
  Serial.println("Paso 2: Pines I2C configurados.");

  sensor.setTimeout(500);
  sensor.init(); 
  Serial.println("Paso 3: Sensor VL6180X inicializado.");

  sensor.configureDefault();
  sensor.startRangeContinuous(100);
  Serial.println("Paso 4: ¡Todo listo! Entrando al Loop...");
}

void loop() {
  // El método de lectura cambia en la librería del VL6180X
  float distancia = sensor.readRangeContinuousMillimeters() / 1000.0; // metros
  
  unsigned long tiempoActual = millis();
  float dt = (tiempoActual - tiempoAnterior) / 1000.0; // segundos

  if (dt > 0) {
    float velocidad = (distancia - distanciaAnterior) / dt; // m/s
    float aceleracion = (velocidad - velocidadAnterior) / dt; // m/s^2

    // Se añaden 3 decimales a la impresión de distancia para apreciar los milímetros
    Serial.print("Distancia(m): "); Serial.print(distancia, 3);
    Serial.print(" | Vel(m/s): "); Serial.print(velocidad);
    Serial.print(" | Acel(m/s^2): "); Serial.println(aceleracion);

    distanciaAnterior = distancia;
    velocidadAnterior = velocidad;
    tiempoAnterior = tiempoActual;
  }
}