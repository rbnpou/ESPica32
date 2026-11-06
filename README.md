### ACLARACIÓN: ESPica32 es un proyecto de hardware abierto creado por el radioaficionado argentino Iván (LU8MIL, del canal de YouTube Radio Bunker). He creado y publicado este repositorio para darle visibilidad al proyecto ya que me ha parecido muy interesante y útil. Me comuniqué con el, pero aún no obtuve respuesta, y como es un proyecto abierto, me tomé este atrevimiento.

Publicado originalmente en https://www.todo-sdr.com/espica32-cliente-kiwisdr/https://www.todo-sdr.com/espica32-cliente-kiwisdr/

Sus redes son 

- https://www.youtube.com/@RADIOBUNKER73
- https://www.qrz.com/db/KN4TOU
- https://x.com/lu8mil
- https://www.instagram.com/radiobunker73

El siguiente README.md fue redactado por el y es parte del proyecto:

# Radio KiwiSDR mínima (ESP32 + OLED + 1 encoder)

Receptor de KiwiSDR por WiFi con lo mínimo: **ESP32 WROOM**, **pantalla OLED**, **un encoder** y **un módulo de audio**.
El ESP32 se conecta al Kiwi como lo haría un navegador (WebSocket, puerto 8073) y saca el audio por el módulo.
El Kiwi se elige desde una **página web del propio ESP32**, abierta desde el móvil u ordenador.

## Material

- ESP32 WROOM (DevKit de 30/38 pines)
- OLED I2C 128x64 (SSD1306 de 0,96" o SH1106 de 1,3")
- 1 encoder con pulsador (EC11 o módulo KY-040)
- Módulo de audio, **uno de estos**:
  - I2S: PCM5102, MAX98357A, UDA1334… (mejor calidad)
  - Amplificador analógico (PAM8403, LM386…) conectado al DAC interno del ESP32
- Cables, protoboard, alimentación por USB

## Conexiones

| Pieza | Pin del módulo | ESP32 |
|---|---|---|
| OLED | VCC / GND | 3V3 / GND |
| OLED | SDA / SCL | GPIO21 / GPIO22 |
| Encoder | A (CLK) / B (DT) | GPIO32 / GPIO33 |
| Encoder | SW (pulsador) | GPIO27 |
| Encoder | + / GND | 3V3 / GND |
| Audio I2S | BCK (BCLK) | GPIO18 |
| Audio I2S | LCK (LRC / WS) | GPIO19 |
| Audio I2S | DIN | GPIO23 |
| Audio I2S | VIN / GND | 5V o 3V3 / GND |

**PCM5102:** los pines FLT, DEMP y FMT a GND, XMT (XSMT) a 3V3 y SCK a GND.
**MAX98357A:** SD a VIN (o sin conectar) para que esté activo.
**Amplificador analógico:** en `src/config.h` pon `AUDIO_INTERNAL_DAC 1`; la señal sale por **GPIO25** (y GPIO26) hacia la entrada del amplificador, con GND común. El DAC interno es de 8 bits: suena a "radio" pero vale para voz.

Si el encoder gira al revés, intercambia A y B. Si salta 2 pasos por clic, pon `ENC_STEPS_PER_DETENT 2`.
Si tu OLED es de 1,3" y sale desplazada o con ruido, pon `OLED_SH1106 1`.

## Compilar y subir (Arduino IDE)

1. Abre `kiwi-radio-mini.ino` (la carpeta debe llamarse igual que el `.ino`).
2. *Herramientas > Placa > Gestor de tarjetas*: instala **esp32 by Espressif Systems, versión 2.0.17** (con la 3.x no compila). Si ya tienes otra versión, elige la 2.0.17 en el gestor.
3. *Herramientas > Gestionar bibliotecas*: instala solo **U8g2** (autor oliver); ya no hace falta la biblioteca WebSockets.
4. Placa: **ESP32 Dev Module**. *Partition Scheme*: **No OTA (2MB APP/2MB SPIFFS)**, porque con el esquema por defecto el programa puede no caber.
5. Elige el puerto y pulsa *Subir*. Si se queda en "Connecting...", mantén pulsado el botón BOOT de la placa hasta que empiece a subir.
6. Monitor serie a 115200 si quieres ver mensajes.

Los ajustes de pines, audio y OLED están en `config.h`.

## Primer arranque

1. Sin WiFi guardada, la pantalla muestra **MODO CONFIGURACIÓN**.
2. Con el móvil, conéctate a la WiFi **KiwiRadio-Setup** (clave `kiwiradio`). Se abre una página (si no, entra en `192.168.4.1`).
3. Escribe tu WiFi, y si quieres ya un servidor Kiwi. Guarda: la radio se reinicia y se conecta.
4. Al arrancar, la pantalla muestra durante 7 s la dirección de la web (una IP, y `kiwiradio.local`).

Para volver a configurar la WiFi: **mantén pulsado el encoder al encender**. Si la WiFi guardada no responde en 20 s, entra sola en modo configuración.

## Página web

**Control desde el navegador** (por si falla el encoder): arriba de la página aparece el panel *Control* con la frecuencia grande, botones − / + (según el paso elegido), un campo para escribir la frecuencia en kHz y pulsar *Ir*, el paso, el modo (LSB/USB/AM/CW/FM), el volumen y atajos de banda (160 m … 10 m, WWV 10 MHz). El panel también tiene el **Buffer de audio**: elige un **mínimo** de 100, 200, 300, 400, 500, 600, 800, 1000, 1500 o 2000 ms (el buffer nunca baja de ese valor; más alto = menos cortes pero más retardo). Con ADPCM cada paquete trae 171 ms de audio, así que por debajo de unos 200 ms basta un pequeño retraso de la red para que se corte. El botón *Subir solo si hay cortes* lo agranda solo, hasta 2 s, cada vez que se corta el audio, y lo baja despacio hasta el mínimo elegido; apágalo para dejarlo fijo. Además el programa compensa la pequeña diferencia entre el reloj de audio del Kiwi y el del ESP32 (quita o repite una muestra de vez en cuando) para que el nivel del buffer no se vacíe con las horas. Se muestra el contenido actual del buffer y el número de cortes. Se sincroniza con la pantalla y con el encoder, y se actualiza cada segundo.

Con la radio en tu WiFi, abre la IP que muestra la pantalla (o `http://kiwiradio.local`):

- **Servidores guardados** (hasta 10): "Usar" cambia de Kiwi al instante, "X" lo borra.
- **Conectar a un Kiwi**: escribe host (o IP), puerto (8073 por defecto), clave si la tiene y un nombre. Se guarda en la lista y se conecta. Puedes pegar la dirección completa (`http://host:8073/`).
- Enlace a la lista de KiwiSDR públicos para copiar el host.
- **WiFi**: cambiar de red (reinicia la radio).
- Arriba hay una línea de estado (servidor, frecuencia, modo, señal) que se actualiza sola.

## Audio comprimido (ADPCM) para Kiwis lejanos

Con un ping alto (200–300 ms o más) el ESP32 no consigue recibir el audio sin comprimir (24 kB/s) en tiempo real: su ventana TCP es pequeña y los datos llegan a ráfagas, así que ningún buffer lo arregla (el diagnóstico lo muestra como *Caudal recibido* por debajo del 100 %). Con **ADPCM** el Kiwi envía 4 veces menos datos (unos 6 kB/s). Está **activado por defecto**; el botón *Audio comprimido ADPCM* del panel Control lo apaga y enciende al instante. Si con ADPCM el audio suena a ruido, apágalo y avísame. A cambio se pierde algo de calidad (4 bits por muestra), que para voz/SSB casi no se nota.

## Diagnóstico de cortes de audio

En la web, panel **Diagnóstico** (se abre pulsándolo; se actualiza cada 2 s). Muestra, desde que se abrió la conexión con el Kiwi:

- **Ping aproximado**: lo que tardó la conexión TCP y el handshake.
- **Paquetes/s** (esperado 23,4) y un **histograma de huecos** entre paquetes: si hay muchos de 250 ms o más, la red entrega a tirones.
- **Buffer** actual, objetivo y mínimo de los últimos 20 s.
- **Cortes** clasificados: *red* (no llegaban paquetes), *ESP32* (el bucle tardó en atender la red) o *buffer/reloj* (llegaban datos pero el buffer se vació).
- **WiFi** (dBm actual y el peor), paquetes descartados y ajustes de reloj.
- Un **veredicto** y los últimos 14 eventos con la hora (también salen por el monitor serie con el prefijo `[dbg]`).

Se puede reiniciar con *Reiniciar contadores*.

## Pantalla

```
Mi Kiwi              S7         <- servidor (o estado) y S-meter en texto
        7100.00                 <- frecuencia en kHz
[ LSB ] [ 1kHz ] [ V40 ]        <- modo | paso | volumen (invertido = lo que controla el encoder)
[██████████            ]        <- S-meter gráfico
```

(He interpretado "RST" como la intensidad de señal, es decir, el S-meter. Si querías otra cosa, dímelo.)

## Control con un solo encoder

| Gesto | Efecto |
|---|---|
| Girar | Actúa sobre la función activa: frecuencia, volumen o modo |
| Clic | Cambia la función activa: frecuencia → volumen → modo → frecuencia |
| Doble clic | Salta a la **siguiente banda** (160 m, 80 m, 40 m, WWV, 30 m, 20 m, 17 m, 15 m, 12 m, 10 m) con su frecuencia y modo habituales; en pantalla aparece "Banda 40m" un momento. Vuelve a la primera tras la 10 m |
| Clic largo (0,6 s) | Cambia el paso de sintonía: 10 Hz, 100 Hz, 1 k, 5 k, 10 k |
| Clic muy largo (2 s) | Muestra 8 s la dirección de la web |

- Tras 4 s sin tocar, el encoder vuelve solo a sintonizar.
- Girando rápido, la frecuencia salta x4 o x10.
- Modos: LSB, USB, AM, CW, FM.
- Se guardan en la flash la WiFi, los servidores, el servidor elegido, la frecuencia, modo, volumen y paso.

## Puntos a verificar al probar (el protocolo está escrito de memoria)

Este código se ha probado con simulaciones en un PC (protocolo WebSocket, decodificador ADPCM frente a una biblioteca de referencia, buffer, web y diagnóstico) y en una placa real, pero **no con todos los Kiwis**. El formato de los paquetes y los comandos conviene contrastarlo con **kiwiclient** (jks-prv/kiwiclient):

- Ruta del WebSocket: se usa `/kiwi/<n>/SND`. Kiwis antiguos usan `/<n>/SND`.
- Autenticación: `SET auth t=kiwi p=#` (`#` = sin clave).
- Paquete `SND`: tras la etiqueta de 3 bytes van flags (1), secuencia (4), S-meter (2, big-endian) y muestras de 16 bits. Con `compression=1` los datos van en IMA-ADPCM (nibble bajo primero, el estado pasa de un paquete al siguiente; el primer paquete solo fija el estado); con `compression=0`, en PCM de 16 bits big-endian.
- Algunos Kiwis públicos expulsan a los oyentes inactivos tras unos minutos; la radio reintenta la conexión cada 5 s.
- Solo funcionan Kiwis por `ws://` (sin TLS).

## Posibles mejoras

- Memorias de frecuencias propias
- Compensar la deriva entre el reloj del Kiwi (12 kHz) y el del ESP32
