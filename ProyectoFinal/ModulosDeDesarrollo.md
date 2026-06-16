# Resumen del Proyecto: Sistema de Telemetría y Control Térmico (ESP32-C6)

Este documento resume los módulos de hardware y la arquitectura de software implementados hasta el momento utilizando el framework **ESP-IDF v6.0**.

---

## 🗺️ Mapa de Asignación de Hardware (Pines GPIO)
Se ha organizado la distribución de pines para evitar colisiones eléctricas en el hardware del ESP32-C6:

| Módulo | Componente | Pin GPIO | Periférico Asociado | Configuración Técnico / API |
| :--- | :--- | :--- | :--- | :--- |
| **Módulo 1** | Termistor NTC 10kΩ | *Asignado internamente* | ADC1 | Calibración por Curve Fitting |
| **Módulo 2** | Ventilador (Motor PWM) | **GPIO 13** | LEDC (Canal 4) | Frecuencia: 5kHz, Resolución: 8 bits |
| **Módulo 2** | LED Rojo de Alarma | **GPIO 12** | LEDC (Canal 5) | Frecuencia: 5kHz, Resolución: 8 bits |
| **Módulo 3** | LED RGB (Canal Rojo) | **GPIO 4** | LEDC (Canal 0) | Frecuencia: Configurable, Resolución: 13 bits |
| **Módulo 3** | LED RGB (Canal Verde) | **GPIO 5** | LEDC (Canal 1) | Frecuencia: Configurable, Resolución: 13 bits |
| **Módulo 3** | LED RGB (Canal Azul) | **GPIO 6** | LEDC (Canal 2) | Frecuencia: Configurable, Resolución: 13 bits |

---

## 🛠️ Estado Actual de los Módulos

### Módulo 1: Núcleo de Telemetría Térmica (Termistor NTC)
* **Estado:** **Completado e Integrado.**
* **Implementación:** La tarea `v_task_ntc_sampler` lee de forma asíncrona los valores crudos del ADC, aplica el filtrado mediante la API Curve Fitting de ESP-IDF v6.0, y realiza la conversión matemática mediante la ecuación del termistor para inyectar la temperatura real en Celsius dentro de la variable global compartida `g_ambient_temperature` cada 1 segundo.

### Módulo 2: Sistema de Ventilación y Alarma Térmica
* **Estado:** **Completado e Integrado.**
* **Implementación:** Se creó el controlador independiente `fan_alarm.c/h`. Utiliza el periférico `LEDC` en modo de baja velocidad (`LEDC_LOW_SPEED_MODE`), el cual es el estándar nativo para chips RISC-V como el ESP32-C6. Responde al lazo de control térmico con tres estados clave basados en la temperatura del Módulo 1.

### Módulo 3: Iluminación Ambiental (LED RGB)
* **Estado:** **Completado, Refactorizado e Integrado.**
* **Implementación:** Se migró el control hacia la arquitectura moderna de `library_led_c.c/h` con resolución de 13 bits (0-8191) eliminando las llamadas a macros obsoletas como `HIGH_SPEED_MODE`. 
* **Capa de Abstracción para Conectividad:** Se portaron las funciones originales de estado de red a la nueva librería. El módulo de Wi-Fi (`wifi_app.c`) interactúa de manera transparente con las funciones heredadas (`rgb_led_wifi_app_started`, `rgb_led_http_server_started`, y `rgb_led_wifi_connected`), alterando los colores del LED RGB de acuerdo al estado de la pila de red sin generar conflictos de Timers ni duplicación de código en el hardware.

---

## 🧠 Arquitectura de Software y Lazo de Control (`main.c`)

El núcleo del programa coordina las operaciones mediante tareas concurrentes administradas por **FreeRTOS**:

1.  **`v_task_ntc_sampler` (Prioridad 5):** Encargada de actualizar de fondo la variable compartida de temperatura cada 1000ms.
2.  **`v_task_thermal_control` (Prioridad 5):** Lazo cerrado de control que despierta cada 500ms para evaluar las condiciones y actuar sobre el hardware físico:
    * **Temperatura $\le$ 25°C:** Ventilador Apagado (0%), Alarma Apagada, LED RGB en rango **Azul**.
    * **Temperatura entre 25°C y 35°C:** Ventilador al 50%, Alarma Apagada, LED RGB en rango **Verde**.
    * **Temperatura entre 35°C y 40°C:** Ventilador al 50%, Alarma Apagada, LED RGB transiciona a rango **Rojo**.
    * **Temperatura > 40°C:** Ventilador a Máxima Potencia (100%), Alarma Activa (LED Rojo Encendido), LED RGB se mantiene en rango **Rojo**.
3.  **Pila de Conectividad (`wifi_app_start`):** Se ejecuta al finalizar la inicialización del hardware local, asegurando que los servicios de red tomen el control visual del LED RGB únicamente durante los eventos de conexión/inicialización y retornen el control térmico al estabilizarse.

El proyecto compila al 100% sin errores de declaración implícita ni solapamiento de memoria Flash (ajustada correctamente a **4MB** en el archivo `sdkconfig`).
