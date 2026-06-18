# Análisis y sustento de modificaciones — STR 2026

## 1. Separación de responsabilidades

**Problema detectado:** `main.c` escribía `g_ambient_temperature` como variable global sin mutex. El servidor web entregaba temperatura simulada y no existía una capa común que sincronizara sensor, actuadores y dashboard.

**Decisión:** Se agregó `control_system.c/.h` como capa central de estado. Todo acceso a temperatura, modos, setpoints, ventilador, servo y RGB pasa por un mutex de FreeRTOS.

**Sustento STR:** En un sistema de tiempo real, la tarea de control no debe depender del servidor web. El control local se inicia antes del Wi-Fi y mantiene periodo de 1 segundo aunque haya clientes HTTP.

## 2. PWM/LEDC sin conflicto de timers

**Problema detectado:** El RGB usaba `LEDC_TIMER_0`. Si el ventilador o el servo compartían ese timer, el servo necesitaba 50 Hz y rompería la frecuencia del RGB/fan.

**Decisión implementada:**

| Subsistema | GPIO | Canal LEDC | Timer | Frecuencia | Resolución |
|---|---:|---:|---:|---:|---:|
| Ventilador PWM | 19 | CH0 | TIMER_0 | 25 kHz | 10 bits |
| RGB R | 21 | CH1 | TIMER_1 | 1 kHz | 8 bits |
| RGB G | 22 | CH2 | TIMER_1 | 1 kHz | 8 bits |
| RGB B | 23 | CH3 | TIMER_1 | 1 kHz | 8 bits |
| Servo cortina | 18 | CH4 | TIMER_2 | 50 Hz | 14 bits |
| LED alarma | 2 | GPIO normal | N/A | 1 s toggle | N/A |

**Sustento:** El servo queda aislado en su timer de 50 Hz. Fan y RGB no comparten timer con el servo. Se usa `LEDC_LOW_SPEED_MODE`, más portable para ESP32-C6.

## 3. Control térmico proporcional real

**Problema detectado:** `/dhtSensor.json` respondía valores fijos `30.1` y `40.5`; el fan no respondía a la NTC.

**Decisión:** La tarea `control_task` lee ADC/NTC, convierte a °C y calcula fan proporcional:

- Temp <= deseada: fan 0 %.
- Temp >= máxima: fan 100 %.
- Entre ambas: interpolación lineal.
- Temp > máxima: LED rojo/alarma activo.

**Sustento:** Cumple el requerimiento del profesor de lazo proporcional automático.

## 4. Protección contra Watchdog Timeout

**Problema detectado:** La función heredada de agenda hacía `vTaskDelay(40000)` cuando coincidía un horario. Eso congelaba esa tarea 40 segundos y podía ocultar eventos de agenda o inducir latencias innecesarias.

**Decisión:** La agenda se movió a `control_system.c`. Ahora se revisa de forma periódica sin bloqueos largos. Cuando un registro coincide, solo se aplica la posición de cortina y la tarea vuelve a dormir 1 segundo.

**Sustento:** En STR, los delays largos dentro de lógica de decisión son mala práctica porque reducen determinismo.

## 5. NVS robusto

**Problema detectado:** Había dos modelos de agenda distintos: strings `reg01` en `wifi_app.c` y blobs en `register.c`. Además `registers.h` tenía un typo `size_size_t`.

**Decisión:** Se consolidó agenda en `register.c/.h` con blobs NVS `reg_01` a `reg_10`, mutex y JSON uniforme. También se agregó NVS para configuración de control (`ctrl_cfg/state_v1`).

**Sustento:** Guardar blobs tipados evita parseos frágiles tipo string concatenado `HHMM1110000`.

## 6. Servidor HTTP escalable

**Problema detectado:** El servidor tenía endpoints incompletos, JSON armado con `sprintf`, POST síncronos en front-end y handlers que no respondían nada.

**Decisión:** Se reemplazó `http_server.c` por endpoints REST simples:

- `GET /api/state`: estado completo del sistema.
- `POST /api/control`: actualiza térmico, cortina y RGB.
- `GET /dhtSensor.json`: compatibilidad, ahora con NTC real.
- `GET /read_regs.json` y `POST /readreg.json`: agenda JSON.
- `POST /regchange.json`: guarda agenda.
- `POST /regerase.json`: borra agenda.
- `POST /wifiConnect.json`: credenciales STA.
- `POST /api/ap_config`: credenciales Soft-AP dinámicas persistidas en NVS.
- `POST /OTAupdate` y `POST /OTAstatus`: OTA.

**Sustento:** Se usa `cJSON` y `httpd_resp_send_err` para errores. Se limita el tamaño de body para evitar abuso de memoria.

## 7. Dashboard web eficiente

**Problema detectado:** El front-end usaba `XMLHttpRequest` síncrono (`false`) en OTA/status y funciones duplicadas. Eso puede congelar el navegador y generar peticiones innecesarias al ESP32.

**Decisión:** Se migró a `fetch` asíncrono. El dashboard consulta `/api/state` cada 2 segundos, grafica temperatura en un canvas local y solo envía cambios cuando el usuario presiona guardar.

**Sustento:** Menos bloqueo, menos tráfico y mejor separación UI/backend.

## 8. CMake corregido

**Problema detectado:** El CMake original no incluía `rgb_led.c` ni `register.c`, aunque el código los usaba. Tampoco embebía explícitamente los archivos web.

**Decisión:** `main/CMakeLists.txt` ahora lista todos los `.c` requeridos y usa `EMBED_FILES` para HTML/CSS/JS/jQuery.

## 9. Limitaciones honestas

No pude compilar con `idf.py build` dentro de este entorno porque no está disponible el toolchain ESP-IDF/ESP32-C6. Sí se hizo revisión estática de estructura, llaves, JS y coherencia de dependencias. Al compilar localmente, revisa especialmente:

- Que tu versión de ESP-IDF use los componentes `esp_driver_gpio` y `esp_driver_ledc`. En ESP-IDF más viejo, podría requerir `driver` en `REQUIRES`.
- Que los GPIO 18/19/21/22/23 estén disponibles en tu placa ESP32-C6 específica.
- Que el pin de ADC definido por `ADC_CHANNEL_0` corresponda al GPIO físico donde conectaste el divisor NTC.
