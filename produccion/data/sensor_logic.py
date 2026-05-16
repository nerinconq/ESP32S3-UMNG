# Configuración de Sensores (Standalone ToF)
# Este archivo es leído por el ESP32 en el arranque y sirve como plantilla
# en el editor web para configurar la lógica dinámica del estudiante.

print("Cargando perfil del sensor ToF...")

# ==========================================
# SELECCIÓN DEL SENSOR (Modelos Soportados)
# ==========================================
# Elige el sensor que tienes conectado físicamente en el bus I2C (Pines 4 y 5).
# 
# Opciones disponibles:
# "VL53L0X"  : Básico, alcance hasta ~2000 mm. Ideal para la mayoría de experimentos (carritos, planos).
# "VL53L1X"  : Largo alcance, hasta ~4000 mm. Excelente para caída libre desde gran altura.
# "VL6180X"  : Corto alcance (Macro), hasta ~600 mm. Ideal para medir péndulos pequeños con alta precisión.
# "VL53L5CX" : Multizona 8x8. El sistema usa la distancia promedio central. Rango hasta ~4000 mm.

nombre = "Cinemática Básica"
sensor = "VL53L0X" # <-- Cambia el nombre aquí según el hardware que vayas a usar

# ==========================================
# CONFIGURACIÓN DEL EXPERIMENTO
# ==========================================
# Variables disponibles para la captura de datos en la interfaz: 
# dist (mm), vel (m/s), acc (m/s²)

# Ajustes sugeridos para la toma de datos:
frecuencia_hz = 100  # Muestreo rápido (100 Hz = 10 ms por muestra) para cinemática detallada.
duracion_s = 10      # Duración típica estimada del experimento en segundos.

# ==========================================
# RANGOS TEÓRICOS (Límites de validación)
# ==========================================
# Estos valores representan los límites operativos fiables según el modelo.
# Puedes utilizarlos en la interfaz para ignorar ruido o "outliers".

if sensor == "VL53L0X":
    dist_min = 30    # mm
    dist_max = 2000  # mm
elif sensor == "VL53L1X" or sensor == "VL53L1X_V2":
    dist_min = 40    # mm
    dist_max = 4000  # mm
elif sensor == "VL6180X":
    dist_min = 10    # mm
    dist_max = 600   # mm
elif sensor == "VL53L5CX":
    dist_min = 20    # mm
    dist_max = 4000  # mm
else:
    # Valores por defecto de seguridad
    dist_min = 0
    dist_max = 2000

print(f"Perfil cargado: {nombre}. Sensor activo configurado: {sensor}.")
print("Sistema listo para medir.")
