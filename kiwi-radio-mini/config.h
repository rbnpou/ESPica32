#pragma once
#include <Arduino.h>

// ================= AUDIO =================
// 0 = modulo I2S externo (PCM5102, MAX98357A, UDA1334...)
// 1 = DAC interno del ESP32 (GPIO25) hacia un amplificador analogico (PAM8403, LM386...)
#define AUDIO_INTERNAL_DAC  0

#define PIN_I2S_BCK    18
#define PIN_I2S_LRCK   19
#define PIN_I2S_DOUT   23

#define KIWI_AUDIO_RATE        12000   // Hz que envia el Kiwi
#define I2S_RATE               48000   // 4 x 12000 (interpolacion lineal)
// El colchon de audio (minimo, 300..2000 ms) y el modo auto se eligen desde la web.
#define AUDIO_RING_BYTES       65536   // capacidad del buffer: ~2,7 s de audio

// ================= OLED (I2C) =================
#define OLED_SH1106    0               // 0 = SSD1306 (0.96"), 1 = SH1106 (1.3")
#define PIN_OLED_SDA   21
#define PIN_OLED_SCL   22

// ================= ENCODER =================
#define PIN_ENC_A      32
#define PIN_ENC_B      33
#define PIN_ENC_SW     27
#define ENC_STEPS_PER_DETENT 4         // EC11/KY-040 normal = 4; si salta 2 por clic, pon 2
#define LONG_PRESS_MS  600             // pulsacion larga: cambia el paso
#define VLONG_PRESS_MS 2000            // muy larga: muestra la direccion de la web

// ================= RED =================
#define PORTAL_SSID  "KiwiRadio-Setup"
#define PORTAL_PASS  "kiwiradio"       // minimo 8 caracteres
#define MDNS_NAME    "kiwiradio"       // http://kiwiradio.local
#define KIWI_IDENT   "ESP32Radio"      // nombre que ve el operador del Kiwi

// ================= WIFI POR DEFECTO =================
// Se usa si en la flash no hay ninguna WiFi guardada (primer arranque).
// Si la guardas desde la web, la guardada tiene prioridad.
#define DEFAULT_WIFI_SSID  "FRITZ!Box 7530 GQ 2.4Ghz"
#define DEFAULT_WIFI_PASS  "12532374208134339904"
