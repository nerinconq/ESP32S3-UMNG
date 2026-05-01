#include "HX711.h"

// Pines de comunicación
const int LOADCELL_DOUT_PIN = 6;
const int LOADCELL_SCK_PIN = 7;

HX711 scale;

// Variables de Calibración
float factorDeEscala = 1.0;
float pesoFiltrado = 0.0;

// Parámetros del Filtro Adaptativo (La "Caja de Cambios")
const float ALPHA_LENTO = 0.15;  // Para estabilizar cuando está quieto (absorbe ruido)
const float ALPHA_RAPIDO = 0.85; // Para reaccionar al instante cuando pones/quitas peso
const float UMBRAL_SALTO = 2.0;  // Diferencia en gramos para cambiar de lento a rápido

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n======================================================");
  Serial.println(" PHYSYS FUERZA V1: CALIBRACIÓN + FILTRO ADAPTATIVO (PRO) ");
  Serial.println("======================================================\n");

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  // --- PASO 1: TARA ---
  Serial.println("PASO 1: Limpia la celda de carga (quita todo el peso).");
  Serial.println(">>> Escribe cualquier letra (ej. 'ok') y presiona ENTER para tarar...");
  
  while(!Serial.available()); 
  while(Serial.available()) Serial.read(); 
  
  scale.tare();
  Serial.println("✓ Tara finalizada. El sistema está en 0.\n");

  // --- PASO 2: LECTURA CRUDA ---
  Serial.println("PASO 2: Pon tu masa de calibración (ej. monedas de 21.42g).");
  Serial.println(">>> Escribe 'ok' y presiona ENTER cuando estén estables...");
  
  while(!Serial.available()); 
  while(Serial.available()) Serial.read(); 

  long lecturaCruda = scale.get_value(20); 
  Serial.print("✓ Lectura cruda promedio obtenida: ");
  Serial.println(lecturaCruda);

  // --- PASO 3: INGRESO DEL PESO Y CÁLCULO ---
  Serial.println("\nPASO 3: Escribe el peso exacto en números (ej. 21.42) y presiona ENTER...");
  
  while(!Serial.available()); 
  float pesoConocido = Serial.parseFloat(); 
  while(Serial.available()) Serial.read();

  factorDeEscala = (float)lecturaCruda / pesoConocido;

  Serial.println("\n================================================");
  Serial.print(" TU FACTOR DE ESCALA ES: ");
  Serial.println(factorDeEscala, 4);
  Serial.println("================================================\n");

  scale.set_scale(factorDeEscala);
  pesoFiltrado = pesoConocido; // Precarga del filtro

  Serial.println("Iniciando registro continuo en 3 segundos...");
  delay(3000);
}

void loop() {
  if (scale.is_ready()) {
    // 1. Lectura del sensor (Sobre-muestreo ligero)
    float pesoCrudo = scale.get_units(3); 

    // 2. LÓGICA DEL FILTRO ADAPTATIVO
    float diferencia = abs(pesoCrudo - pesoFiltrado);
    float alphaActual;

    // Si la diferencia es mayor al umbral, metemos "el acelerador"
    if (diferencia > UMBRAL_SALTO) {
      alphaActual = ALPHA_RAPIDO; 
      Serial.print("[RÁPIDO] "); // Solo para que veas cuándo cambia
    } else {
      alphaActual = ALPHA_LENTO;
      Serial.print("[LENTO]  ");
    }

    // Aplicamos la fórmula EMA con el alpha elegido
    pesoFiltrado = (alphaActual * pesoCrudo) + ((1.0 - alphaActual) * pesoFiltrado);

    // 3. Banda Muerta (Fuerza a 0 si es ruido residual muy pequeño)
    if (abs(pesoFiltrado) < 0.5) {
      pesoFiltrado = 0.0;
    }

    float fuerzaNewtons = (pesoFiltrado / 1000.0) * 9.80665;

    Serial.print("Tiempo(ms): "); Serial.print(millis());
    Serial.print(" | RAW(g): ");    Serial.print(pesoCrudo, 2);
    Serial.print(" | FILTRADO(g): "); Serial.print(pesoFiltrado, 2);
    Serial.print(" | FUERZA(N): "); Serial.println(fuerzaNewtons, 4);
  }
  
  delay(100); 
}