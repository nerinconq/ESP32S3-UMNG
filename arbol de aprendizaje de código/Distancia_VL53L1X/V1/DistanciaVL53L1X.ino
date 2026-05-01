#include <Wire.h>
#include <VL53L1X.h>

VL53L1X sensor;
unsigned long tiempoAnterior = 0;
float distanciaAnterior = 0;
float velocidadAnterior = 0;

void setup() {
  Serial.begin(115200);
  delay(2000); 
  
  Serial.println("\n--- INICIANDO ESP32 ---");
  Serial.println("Paso 1: USB Nativo funcionando.");

  Wire.begin(4, 5); 
  Serial.println("Paso 2: Pines I2C configurados.");

  sensor.setTimeout(500);
  if (!sensor.init()) {
    Serial.println("Fallo al iniciar VL53L1X!");
    while (1);
  }
  Serial.println("Paso 3: Sensor VL53L1X inicializado.");

  // Configuración específica para VL53L1X (Rango largo hasta 4m)
  sensor.setDistanceMode(VL53L1X::Long);
  sensor.setMeasurementTimingBudget(50000);
  sensor.startContinuous(50);
  Serial.println("Paso 4: ¡Todo listo! Entrando al Loop...");
}

void loop() {
  // Lectura del VL53L1X
  float distancia = sensor.read() / 1000.0; // Conversión a metros
  
  unsigned long tiempoActual = millis();
  float dt = (tiempoActual - tiempoAnterior) / 1000.0; 

  if (dt > 0) {
    float velocidad = (distancia - distanciaAnterior) / dt; 
    float aceleracion = (velocidad - velocidadAnterior) / dt; 

    distanciaAnterior = distancia;
    velocidadAnterior = velocidad;
    tiempoAnterior = tiempoActual;

    Serial.print("D: "); Serial.print(distancia, 3); Serial.print(" m\t");
    Serial.print("V: "); Serial.print(velocidad, 3); Serial.print(" m/s\t");
    Serial.print("A: "); Serial.print(aceleracion, 3); Serial.println(" m/s^2");
  }
}