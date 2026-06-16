# Resumen del Proyecto: Sistema de Telemetría y Control Térmico (ESP32-C6)

Este documento resume los módulos de hardware y la arquitectura de software de la aplicación de control climático implementada bajo el framework **ESP-IDF v6.0**.

---

## 🗺️ Mapa de Asignación de Hardware (Pines GPIO)
Se ha consolidado la distribución de pines para evitar colisiones eléctricas y asegurar la compatibilidad con los periféricos nativos del chip RISC-V del ESP32-C6:

| Módulo | Componente | Pin GPIO | Periférico Asociado | Configuración Técnico / API |
| :--- | :--- | :--- | :--- | :--- |
| **Módulo 1** | Termistor NTC 10kΩ | *Asignado internamente* | ADC1 | Calibración por Curve Fitting |
| **Módulo 2** | Ventilador (Motor PWM) | **GPIO 13** | LEDC (Canal 4) | Frecuencia: 5kHz, Resolución: 8 bits, Baja Velocidad |
| **Módulo 2** | LED Rojo de Alarma | **GPIO 12** | LEDC (Canal 5) | Frecuencia: 5kHz, Resolución: 8 bits, Baja Velocidad |
| **Módulo 3** | LED RGB (Canal Rojo) | **GPIO 4** | LEDC (Canal 0) | Frecuencia: Configurable, Resolución: 13 bits |
| **Módulo 3** | LED RGB (Canal Verde) | **GPIO 5** | LEDC (Canal 1) | Frecuencia: Configurable, Resolución: 13 bits |
| **Módulo 3** | LED RGB (Canal Azul) | **GPIO 6** | LEDC (Canal 2) | Frecuencia: Configurable, Resolución: 13 bits |
| **Módulo 4** | Servo Cortina (PWM) | *Definido en servo_curtain.h* | LEDC / MCPWM | Control de posición de cortinas automáticas |

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

### Módulo 4: Automatización de Persianas (Servo Cortina)
* **Estado:** **Completado e Integrado.**
* **Implementación:** Se acopló directamente al lazo de control térmico en `main.c`. En modo automático, si la temperatura ambiente supera el umbral crítico definido como `g_max_temperature - 8.0f`, el servo cierra las cortinas (posición `0`) para mitigar el impacto de la radiación térmica exterior. Si el clima es fresco, las abre por completo (posición `100`). Posee una bandera de anulación manual (`servo_get_manual_override()`) para ceder el control al usuario a través de comandos externos.

### Módulo 5: Gestión de Almacenamiento No Volátil (NVS) y Calendario
* **Estado:** **Completado, Modularizado e Integrado.**
* **Implementación:** Este módulo se estructuró en dos subsistemas completamente desacoplados para cumplir con las buenas prácticas de arquitectura de software:
  1. **Ajustes del Sistema (`settings.c/h`):** Recupera y almacena en memoria Flash las variables dinámicas del termostato (`g_target_temperature` y `g_max_temperature`) utilizando operaciones en formato BLOB para asegurar compatibilidad estricta.
  2. **Planificador Horario (`schedule.c/h`):** Gestiona un arreglo dinámico de hasta 10 temporizadores (`timer_register_t`) para la apertura/cierre de cortinas. Implementa funciones que traducen estas estructuras a cadenas JSON (`registers_get_json`) optimizadas para ser consumidas de forma asíncrona por el servidor HTTP nativo.

---

## 🧠 Arquitectura de Software y Lazo de Control (`main.c`)

El núcleo del programa coordina las operaciones mediante tareas concurrentes administradas por **FreeRTOS**:

1. **Inicialización y Carga de NVS:** Al arrancar en `app_main`, el microcontrolador llama a `registers_init()` para mapear las alarmas horarias desde la flash, y extrae los últimos límites de temperatura parametrizados por el usuario utilizando `settings_get_target_temperature()` y `settings_get_max_temperature()`.
2. **`v_task_ntc_sampler` (Prioridad 5):** Encargada de actualizar de fondo la variable compartida de temperatura cada 1000ms.
3. **`v_task_thermal_control` (Prioridad 5):** Lazo cerrado de control que despierta cada 500ms para evaluar las condiciones ambientales actuales y actuar sobre el hardware físico aplicando la siguiente lógica adaptativa:
    * **Temperatura $\le$ `g_target_temperature`:** Ventilador Apagado (0%), Alarma Apagada, LED RGB en rango **Azul**, Cortina abierta al 100%.
    * **Temperatura entre `g_target_temperature` y `g_max_temperature`:** Ventilador al 50%, Alarma Apagada, LED RGB transiciona dinámicamente de **Verde** a **Rojo** (según se acerque al límite superior). Cortina abierta al 100% (a menos que descienda por debajo de `g_max_temperature - 8.0f`).
    * **Temperatura > `g_max_temperature`:** Ventilador a Máxima Potencia (100%), Alarma Activa (LED Rojo Encendido), LED RGB se mantiene en rango **Rojo**, Cortina cerrada al 0% para protección solar.
4. **Pila de Conectividad (`wifi_app_start`):** Se ejecuta al finalizar la inicialización del hardware local, asegurando que los servicios de red tomen el control visual del LED RGB únicamente durante los eventos de conexión/inicialización y retornen el control térmico de forma segura al estabilizarse.

---

## 🚨 Problemas Más Críticos Resueltos (Lecciones Aprendidas)

Durante el proceso de desarrollo e integración final, se identificaron y solventaron tres fallas estructurales severas:

### 1. El Conflicto de la Declaración Implícita por Enlaces Cruzados
* **Problema:** Al intentar separar de forma modular la lógica de las alarmas horarias y la del termostato, los prototipos de funciones se cruzaron. Archivos principales como `main.c` no lograban localizar los archivos de cabecera correctos para obtener las firmas de las funciones de temperatura, arrojando errores masivos de compilación por declaraciones implícitas (`implicit declaration of function`).
* **Solución:** Se aislaron por completo las responsabilidades. Se definió una interfaz limpia en
