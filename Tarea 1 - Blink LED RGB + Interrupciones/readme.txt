# Control de LED RGB con Botón mediante Interrupciones y FreeRTOS  
**ESP32-C6 WROOM DevKitC-1 (MuseLab N16) - Sistemas en Tiempo Real**

## Descripción del Proyecto

Este proyecto implementa un **sistema embebido en tiempo real** que controla el LED RGB integrado (WS2812) de la placa ESP32-C6 mediante un botón físico.  

Cada pulsación del botón cambia cíclicamente entre 5 estados de parpadeo:

- **LED_2_2**: 2 segundos encendido (Azul) / 2 segundos apagado  
- **LED_2_1**: 2 segundos encendido (Verde) / 1 segundo apagado  
- **LED_1_1**: 1 segundo encendido (Rojo) / 1 segundo apagado  
- **LED_05_05**: 0.5 segundos encendido (Morado) / 0.5 segundos apagado  
- **LED_OFF**: LED permanentemente apagado  

El sistema utiliza **interrupciones GPIO** + **FreeRTOS** para garantizar **baja latencia** y cumplimiento de deadlines estrictos.

## Características Principales

- Interrupción en flanco de bajada (`GPIO_INTR_NEGEDGE`) en GPIO9 (Botón BOOT)
- ISR optimizada con `IRAM_ATTR` y `xQueueSendFromISR`
- Control preciso del WS2812 mediante periférico **RMT** a 10 MHz
- Tarea `led_task` con prioridad alta (preemptiva)
- Comunicación entre ISR y tarea mediante cola FreeRTOS
- Parpadeo cíclico correcto (el LED continúa blinkeando aunque no se pulse el botón)
- Código compatible con **ESP-IDF v5.3** y componente `led_strip v3.0.3`

## Hardware Utilizado

- **Placa**: ESP32-C6 WROOM DevKitC-1 N16 (MuseLab) – pines soldados
- **LED RGB**: WS2812 integrado conectado a **GPIO8**
- **Botón**: BOOT conectado a **GPIO9** (pull-up interna)
- **Entorno de desarrollo**: Windows + ESP-IDF v5.3 + VS Code

**Nota**: GPIO8 y GPIO9 son pines de strapping, pero la placa permite su uso normal después del boot.

## Estructura del Proyecto
esp32-c6-led-button-rt/
├── main/
│   └── main.c                 ← Código fuente principal
├── CMakeLists.txt
├── idf_component.yml          ← Dependencia del led_strip
├── sdkconfig.defaults
└── README.md
