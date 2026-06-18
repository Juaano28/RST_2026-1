# STR 2026 — Sistema de Control Ambiental Automatizado ESP32-C6

Proyecto final de Sistemas de Tiempo Real para controlar temperatura, ventilación, iluminación RGB y cortinas automatizadas desde un ESP32-C6 con dashboard web, Wi-Fi AP+STA, agenda persistente y OTA.

## Resumen funcional

El firmware implementa una arquitectura por capas:

1. **Capa de adquisición:** ADC oneshot + calibración Curve Fitting para NTC 10 kΩ.
2. **Capa de control local:** tarea FreeRTOS periódica de 1 segundo en `control_system.c`.
3. **Capa de actuadores:** ventilador PWM, LED RGB, servo 50 Hz y LED rojo de alarma.
4. **Capa persistente:** NVS para setpoints, modos, RGB, cortina y 10 registros de agenda.
5. **Capa de red:** Wi-Fi AP+STA y SNTP.
6. **Capa web:** dashboard embebido con endpoints JSON y OTA.

## GPIOs usados

| Función | GPIO | Periférico | Notas |
|---|---:|---|---|
| NTC 10 kΩ | ADC1 CH0 | ADC | Divisor: 3.3 V → R serie 10 kΩ → Vout → NTC → GND |
| Potenciómetro opcional | ADC1 CH1 | ADC | Disponible en `adc_module.c` |
| Ventilador | 19 | LEDC CH0 / TIMER_0 | PWM 25 kHz, 10 bits |
| Servo cortina | 18 | LEDC CH4 / TIMER_2 | PWM 50 Hz, pulso 1–2 ms |
| LED RGB rojo | 21 | LEDC CH1 / TIMER_1 | PWM 1 kHz |
| LED RGB verde | 22 | LEDC CH2 / TIMER_1 | PWM 1 kHz |
| LED RGB azul | 23 | LEDC CH3 / TIMER_1 | PWM 1 kHz |
| LED alarma rojo | 2 | GPIO | Parpadea si T > máxima |

> Revisa que estos GPIO existan y estén libres en tu placa ESP32-C6 concreta.

## Diagrama de conexión básico

```text
3.3V ── R fija 10k ──┬── GPIO ADC1_CH0
                     │
                    NTC 10k
                     │
                    GND

GPIO19 ── Driver/MOSFET ── Ventilador ── Fuente externa
GPIO18 ── Señal servo; servo con 5V externo y GND común
GPIO21 ── Resistencia ── RGB Rojo
GPIO22 ── Resistencia ── RGB Verde
GPIO23 ── Resistencia ── RGB Azul
GPIO2  ── Resistencia ── LED alarma rojo
```

Para el ventilador y el servo, usa fuente externa si el consumo supera lo que entrega la placa. Une siempre el GND de la fuente externa con el GND del ESP32-C6.

## Dashboard web

Conéctate al Soft-AP del ESP32 y abre:

```text
http://192.168.0.1/
```

Desde el panel puedes:

- Ver temperatura real, ADC raw, mV, ventilador y alarma.
- Cambiar modo térmico automático/manual.
- Ajustar temperatura deseada y máxima.
- Forzar ventilador manual.
- Cambiar modo de cortina manual/automático por agenda.
- Ajustar apertura del servo.
- Controlar RGB e intensidad.
- Crear, borrar y consultar 10 registros de agenda.
- Guardar credenciales Wi-Fi STA.
- Cambiar credenciales del Soft-AP.
- Subir firmware OTA `.bin`.

## Endpoints principales

| Endpoint | Método | Uso |
|---|---|---|
| `/api/state` | GET | Estado completo del sistema |
| `/api/control` | POST | Actualiza térmico, cortina y RGB |
| `/dhtSensor.json` | GET | Compatibilidad; devuelve NTC real |
| `/read_regs.json` | GET | Lee agenda |
| `/readreg.json` | POST | Lee agenda, compatibilidad |
| `/regchange.json` | POST | Guarda registro de agenda |
| `/regerase.json` | POST | Borra registro |
| `/wifiConnect.json` | POST | Configura Wi-Fi STA |
| `/api/ap_config` | POST | Configura Soft-AP dinámico |
| `/OTAupdate` | POST | Sube firmware OTA |
| `/OTAstatus` | POST | Estado OTA |

## Compilación y carga

Desde la raíz del proyecto:

```bash
idf.py set-target esp32c6
idf.py menuconfig
idf.py build
idf.py -p <PUERTO> flash monitor
```

Ejemplo:

```bash
idf.py -p COM5 flash monitor
```

## Archivos importantes

| Archivo | Descripción |
|---|---|
| `main/main.c` | Inicializa NVS, control local y Wi-Fi |
| `main/control_system.c` | Estado thread-safe, control fan/servo/RGB, agenda |
| `main/http_server.c` | Servidor HTTP, JSON, OTA |
| `main/register.c` | Agenda persistente en NVS |
| `main/adc_module.c` | ADC calibrado Curve Fitting |
| `main/ntc_module.c` | Conversión NTC a °C |
| `main/wifi_app.c` | AP+STA y SNTP |
| `main/index.html`, `app.js`, `app.css` | Dashboard |
| `ANALISIS_MODIFICACIONES.md` | Justificación técnica de cada cambio |

## Nota de validación

Este paquete fue ajustado por revisión estática. Debes hacer la validación final con `idf.py build` en tu entorno ESP-IDF porque aquí no está disponible el toolchain ESP32-C6.
