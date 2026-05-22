# Sistemas en Tiempo Real - 2026-  
**Universidad Nacional De Colombia**

Repositorio oficial de la asignatura **Sistemas en Tiempo Real**.

## Descripción de la Materia

La asignatura **Sistemas en Tiempo Real** aborda el diseño, implementación y análisis de sistemas embebidos donde el **tiempo es un factor crítico**. Se estudian conceptos como scheduling en FreeRTOS, interrupciones de baja latencia, manejo de deadlines estrictos, sincronización de tareas y optimización de timing en periféricos hardware.

### Objetivos del Curso
- Entender los principios de los sistemas de tiempo real (Hard Real-Time y Soft Real-Time)
- Dominar el uso de FreeRTOS en entornos embebidos
- Implementar interrupciones eficientes (`IRAM_ATTR`, ISR-safe)
- Garantizar deadlines estrictos en proyectos reales
- Analizar jitter, latencia y estabilidad del sistema

## Proyectos Desarrollados

### 1. Control de LED RGB con Botón (Proyecto Actual)
- **Placa**: ESP32-C6 WROOM DevKitC-1 N16 (MuseLab)
- **Técnicas utilizadas**:
  - Interrupciones GPIO en flanco de bajada (`GPIO_INTR_NEGEDGE`)
  - ISR optimizada con `IRAM_ATTR` y `xQueueSendFromISR`
  - Control preciso del WS2812 mediante periférico **RMT** (10 MHz)
  - Tarea de alta prioridad con FreeRTOS
  - Comunicación segura entre ISR y tarea mediante cola
- **Estado**: Completado y funcional

### 2. Próximos Laboratorios (en desarrollo)


## Entorno de Desarrollo

- **Placa principal**: ESP32-C6 MuseLab N16
- **Framework**: ESP-IDF v5.3
- **Sistema operativo**: Windows 11 + VS Code + ESP-IDF Extension
- **Simulación**: Fedora 43 Toolbox
- **Componentes clave**: `led_strip v3.0.3`, FreeRTOS, RMT, GPIO
