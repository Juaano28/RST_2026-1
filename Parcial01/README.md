# Sistema de Control de Temperatura RGB con FreeRTOS (v2.0 Extended)

Este proyecto implementa un sistema embebido multitarea utilizando **FreeRTOS** en un microcontrolador **ESP32-C6**. El sistema lee la temperatura ambiente mediante un termistor NTC y permite interactuar con el usuario a través de un potenciómetro, un botón físico, una consola UART interactiva y dos luces LED RGB con diferentes lógicas de activación.

---

## 🎯 Objetivo del Proyecto

El objetivo principal es diseñar e implementar una arquitectura de software robusta, concurrente y eficiente utilizando un sistema operativo en tiempo real (RTOS). Se busca resolver problemas típicos de sistemas embebidos como:
* La gestión de recursos compartidos (concurrencia en periféricos como el ADC).
* La comunicación inter-tareas eficiente (mediante variables atómicas).
* El control preciso de actuadores mediante PWM (LEDC).
* La calibración avanzada de señales analógicas (`Curve Fitting` en ESP32-C6).

---

## 🛠️ ¿Qué se realizó? (Características Principales)

1.  **Monitoreo y Conversión NTC:** Lectura constante de temperatura y conversión matemática mediante el modelo Beta de Steinhart-Hart.
2.  **Control de LED#1 (Cátodo Común):** Activación por rangos independientes de temperatura configurables por comandos UART para cada canal (R, G, B). Permite solapamiento de colores.
3.  **Control de LED#2 (Ánodo Común):** Actúa como alerta visual. Se enciende en color ROJO si la temperatura actual supera un umbral dinámico (0-100 °C) definido en tiempo real por el potenciómetro.
4.  **Interfaz de Usuario Física:** Un botón conectado a un pin GPIO configurado con *pull-up* interno que cicla cíclicamente la unidad de temperatura global entre Celsius (°C), Fahrenheit (°F) y Kelvin (K).
5.  **Consola de Comandos UART:** Procesamiento de comandos para configurar los rangos de temperatura de los LEDs y habilitar la impresión periódica de datos.

---

## 🏗️ ¿Cómo se realizó? (Arquitectura de Software)

El sistema se divide en **tres tareas concurrentes** con prioridades asignadas estratégicamente para garantizar la reactividad del sistema:


### 📊 Distribución de Tareas

| Tarea | Prioridad | Período | Función Principal |
| :--- | :---: | :---: | :--- |
| **`task_led2_btn`** | 3 (Alta) | 20 ms | Monitorea el botón (antirebote), lee el potenciómetro y opera el LED#2 de alerta. |
| **`task_led1_temp`** | 2 (Media) | 2000 ms | Lee el NTC, publica el voltaje y actualiza el color del LED#1 según los rangos establecidos. |
| **`task_uart`** | 1 (Baja) | 20 ms | Escucha y procesa comandos desde la terminal serie. |

### 🔒 Sincronización y Recursos Compartidos

* **Exclusión Mutua (Mutex):** El convertidor analógico-digital (ADC) es un recurso compartido críptico usado por varias tareas. Está protegido mediante el Mutex `s_adc_mutex`. Si una tarea no logra tomarlo en un tiempo límite (*timeout*), libera el procesador para evitar bloqueos.
* **Comunicación Atómica:** Para enviar el voltaje del NTC desde la tarea de temperatura hacia la tarea del LED#2, se utiliza la variable `s_shared_ntc_mv` declarada como `volatile int32_t`. En la arquitectura RISC-V de 32 bits del ESP32-C6, las lecturas y escrituras de 32 bits son nativamente **atómicas**, eliminando la necesidad de un Mutex adicional para este intercambio de datos rápido.

---

## 💡 Conceptos Clave

* **Modelo Beta de Steinhart-Hart:** Ecuación utilizada para modelar la relación no lineal entre la resistencia del termistor NTC y la temperatura absoluta (en Kelvin):
    $$\frac{1}{T} = \frac{1}{T_0} + \frac{1}{\beta} \cdot \ln\left(\frac{R}{R_0}\right)$$
* **PWM con LEDC (Ánodo vs. Cátodo Común):**
    * *Cátodo Común (LED#1):* Un ciclo de trabajo alto (`DUTY_MAX`) significa máxima intensidad lumínica.
    * *Ánodo Común (LED#2):* Funciona con lógica inversa. Para lograr el máximo brillo, el canal del temporizador debe configurarse al mínimo voltaje, cumpliendo la relación: $\text{duty\_ledc} = \text{DUTY\_MAX} - \text{duty\_intuitivo}$.
* **Calibración ADC por Ajuste de Curva (Curve Fitting):** El ESP32-C6 lee una curva polinómica grabada de fábrica en los eFuses para compensar la falta de linealidad natural del ADC en la lectura de milivoltios.

---

## 🔑 Funciones Claves del Sistema

* **`task_led1_temp()`**: Orquestador de la lectura periódica del sensor de temperatura y de mapear el comportamiento térmico en el LED#1.
* **`button_led_module_process()`**: Realiza la lectura del potenciómetro para calcular el umbral actual (escalado de 0 a 100 °C), gestiona el filtro de antirebote (*debouncing*) del botón por software y evalúa la condición de alarma para el LED#2.
* **`ntc_voltage_to_celsius()`**: Función matemática encargada de realizar el despeje del divisor de tensión y aplicar el modelo Beta para retornar valores legibles en grados Celsius.
* **`xTaskCreate()`**: Función de la API de FreeRTOS encargada de instanciar cada una de las tareas asignándoles su respectivo stack, parámetros de configuración y prioridad en el planificador (*scheduler*).
