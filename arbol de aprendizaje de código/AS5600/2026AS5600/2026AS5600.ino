#include <Wire.h>
#include <AS5600.h>

AS5600 encoder;

// Constantes exactas del main.cpp
#define AS5600_RAW_TO_DEGREES 0.087890625
#define DEG_TO_RAD 0.01745329251

unsigned long tiempoAnterior = 0;
float anguloTotal = 0;      
float rawAnteriorRad = 0;   
float velAngularAnt = 0;    

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n--- PHYSYS ANGULAR V1: INICIO ---");
  Wire.begin(10, 11); // Pines I2C para Ángulo

  if (!encoder.isConnected()) {
    Serial.println("¡ERROR! AS5600 no detectado en pines 10/11.");
    while(1); 
  }
  
  // Lectura inicial (Conversión paso a paso)
  float rawInitial = encoder.readAngle();
  rawAnteriorRad = (rawInitial * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;

  Serial.println("Sensor listo. Registrando datos...");
}

void loop() {
  float rawAngle = encoder.readAngle();
  
  // Conversión explícita para claridad pedagógica
  float actualRad = (rawAngle * AS5600_RAW_TO_DEGREES) * DEG_TO_RAD;
  
  // Algoritmo Unwrap
  float deltaAng = actualRad - rawAnteriorRad;
  if (deltaAng > PI) deltaAng -= 2.0 * PI;
  if (deltaAng < -PI) deltaAng += 2.0 * PI;
  
  anguloTotal += deltaAng;
  rawAnteriorRad = actualRad;

  unsigned long tiempoActual = millis();
  float dt = (tiempoActual - tiempoAnterior) / 1000.0;

  if (dt > 0) {
    float omega = deltaAng / dt; // rad/s
    float alpha = (omega - velAngularAnt) / dt; // rad/s^2

    // Registro completo para análisis
    Serial.print("Tiempo(ms):"); Serial.print(tiempoActual);
    Serial.print(",Ang(rad):"); Serial.print(anguloTotal, 3);
    Serial.print(",Vel(rad/s):"); Serial.print(omega, 3);
    Serial.print(",Acel(rad/s^2):"); Serial.println(alpha, 3);

    velAngularAnt = omega;
    tiempoAnterior = tiempoActual;
  }
  delay(10);
}