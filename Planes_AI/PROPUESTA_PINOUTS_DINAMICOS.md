# 🗺️ Propuesta: Propuesta de Pines Dinámicos por Perfiles de Hardware

**Para:** UMNG Physics Lab — Plataforma Kinematics v9.3  
**Estado:** 💡 *En espera de aprobación técnica por parte del Profesor Nelson*  

---

## 1. Contexto y Objetivos

Con la exitosa resurrección de la tarjeta **ESP32-S3-CAM** y la comprobación empírica de que utiliza la disposición física de la **cámara de Freenove**, nos enfrentamos a un nuevo reto arquitectónico:
> Diferentes grupos de estudiantes pueden tener diferentes tarjetas de desarrollo (tarjetas base sin cámara, placas Freenove CAM, o clones de AliExpress tipo AI-Thinker). 

Si usamos compilación estática (`#define`), tendríamos que compilar y mantener versiones de firmware separadas para cada grupo, lo cual dificulta enormemente el mantenimiento.

### El Objetivo:
Crear un **Firmware Adaptativo Único** que detecte e inicialice los sensores según el perfil físico de la placa que tenga cada grupo, configurable de forma inalámbrica desde el navegador y guardado de forma permanente en la memoria no volátil del chip (NVS).

---

## 2. Los Conflictos Físicos a Resolver

El bus de la cámara bloquea un gran número de pines. A continuación vemos el mapa de colisión según el perfil seleccionado:

| Sensor | Asignación Actual (v9.2) | Conflicto con Freenove CAM | Conflicto con AI-Thinker CAM |
| :--- | :--- | :--- | :--- |
| **ToF I2C (SDA/SCL)** | GPIO 4 / GPIO 5 | ❌ **Sí** (Ocupado por SCCB SDA/SCL de cámara) |  (Libres) |
| **Encoder I2C (SDA/SCL)** | GPIO 10 / GPIO 11 | ❌ **Sí** (Ocupado por Cámara D5 y D2) |  (Libres) |
| **Celda HX711 (DT/SCK)** | GPIO 6 / GPIO 7 | ❌ **Sí** (Ocupado por VSYNC y HREF de cámara) |  (Libres) |

Si un grupo tiene la tarjeta de la cámara físicamente conectada, **es imposible usar los pines actuales**. Necesitamos reubicarlos de manera inteligente.

---

## 3. La Solución Propuesta: Sistema Dinámico de Perfiles

Se propone eliminar la definición estática de pines en compilación y migrar a variables dinámicas en tiempo de ejecución gobernadas por la biblioteca **`Preferences`** de ESP32 (guardadas en NVS Flash).

```mermaid
graph TD
    A[Arranque del ESP32-S3] --> B[Leer NVS: Perfil Seleccionado]
    B --> C{¿Qué perfil es?}
    
    C -- 0: Base Devkit --> D[Asignar Pines Originales <br> ToF: 4/5 | Encoder: 10/11 | HX711: 6/7]
    C -- 1: Freenove CAM --> E[Asignar Pines Libres Freenove <br> ToF: 1/2 | Encoder: 3/14 | HX711: 21/26]
    C -- 2: Custom / Manual --> F[Cargar pines personalizados desde memoria NVS]

    D & E & F --> G[Inicializar Buses I2C y Sensores]
    G --> H[Dispositivo Listo e Interfaz Web Activa]
    H --> I[Usuario cambia perfil en PWA Settings]
    I --> J[Guardar en NVS + Auto-Reiniciar]
    J --> A
```

---

## 4. Perfiles Pre-configurados Propuestos

Para facilitarle la vida a los estudiantes, la interfaz web tendrá 3 botones de "Un Solo Clic":

### Perfil 0: Tarjeta Base (Original)
*   **Ideal para:** Placas de desarrollo estándar sin módulo de cámara.
*   **Pines:** ToF en 4/5, Encoder en 10/11, Celda de carga en 6/7.

### Perfil 1: Freenove S3-CAM (¡Probado Hoy!)
*   **Ideal para:** La tarjeta con cámara Freenove.
*   **Pines reubicados en canales libres del header:**
    *   `TOF_SDA` = GPIO 1
    *   `TOF_SCL` = GPIO 2
    *   `ENC_SDA` = GPIO 3
    *   `ENC_SCL` = GPIO 14
    *   `HX711_DT` = GPIO 21
    *   `HX711_SCK` = GPIO 26

### Perfil 2: Personalizado (Manual)
*   Permite a un usuario avanzado seleccionar individualmente cualquier pin GPIO libre desde un dropdown gráfico en los Settings de la Web App.

---

## 5. Mecanismo de Seguridad Anti-Bloqueos (Failsafe)

> **Pregunta Crítica:** *¿Qué pasa si un estudiante cambia a un perfil con pines erróneos y la tarjeta se queda colgada o bloquea el bus I2C?*

### Implementación del Botón Failsafe:
En la primera línea de `setup()`, el firmware verificará si el **botón físico BOOT (GPIO 0)** de la placa se mantiene presionado durante **3 segundos**. 
Si se presiona, el sistema:
1. Formateará las preferencias en la memoria NVS.
2. Encenderá el LED RGB en color **Fucsia/Violeta**.
3. Reestablecerá el Perfil 0 (Tarjeta Base) por defecto.
4. Reiniciará el chip de forma segura.
*¡Esto evita tener que volver a flasear la tarjeta por cable si se introduce una mala configuración!*

---

## 6. Integración en el Dashboard Web (PWA)

En la sección de configuración (⚙️ Settings), se agregará una tarjeta estética e intuitiva:

```text
╔═══════════════════════════════════════════════════════════════╗
║ 🛠️ CONFIGURACIÓN DE HARDWARE Y PINOUT                         ║
╠═══════════════════════════════════════════════════════════════╣
║ Selecciona la tarjeta que estás usando físicamente:           ║
║                                                               ║
║ [ 📱 Tarjeta Base (Sin Cámara) ]  -> Pines: 4,5 / 10,11 / 6,7 ║
║ [ 📷 Freenove S3-CAM (Con Cám) ]  -> Pines: 1,2 / 3,14 / 21,26║
║ [ ⚙️ Configuración Personalizada ]                            ║
║                                                               ║
║  🚨 Nota: El cambio guardará los datos en NVS y reiniciará   ║
║  el laboratorio de bolsillo para aplicar los cambios.         ║
║                                                               ║
║                            [ Guardar y Reiniciar Dispositivo ] ║
╚═══════════════════════════════════════════════════════════════╝
```

---

## 7. Próximos Pasos Técnicos

1. **Tu veredicto:** Revisa este plan de asignación de pines y perfiles dinámicos.
2. **Implementación de bajo impacto:** Cuando sea aprobado, se integrará el stack de `Preferences` y el mapeo en `src/main.cpp` sin alterar en lo absoluto la lógica de medición cinemática, los filtros ni la integración con Desmos.

---
*Fin de la propuesta técnica. Esperando respuesta para actuar.*
