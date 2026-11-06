#pragma once
#include <Arduino.h>

enum { MODE_LSB, MODE_USB, MODE_AM, MODE_CW, MODE_FM, MODE_COUNT };
enum { F_FREQ, F_VOL, F_MODE, FOCUS_COUNT };   // funcion que controla el encoder

struct ModeDef { const char* name; const char* kiwi; int lo; int hi; };

extern const ModeDef MODES[MODE_COUNT];
#define STEP_COUNT 5
extern const uint32_t STEPS[STEP_COUNT];

// Bandas para los botones de la web (al elegirla se pone su frecuencia y modo habituales)
struct BandDef { const char* name; uint32_t khz; uint8_t mode; };
#define BAND_COUNT 10
extern const BandDef BANDS[BAND_COUNT];
int bandIndexFor(uint32_t hz);   // banda en la que esta la frecuencia (-1 = por debajo de la primera)

struct RadioState {
  volatile uint32_t freqHz      = 7100000;
  volatile uint8_t  mode        = MODE_LSB;
  volatile uint8_t  stepIdx     = 1;
  volatile uint8_t  volume      = 40;        // 0..100
  volatile uint8_t  focus       = F_FREQ;
  volatile uint16_t bufFloorMs  = 400;      // buffer de audio: minimo (ms), no baja de este valor
  volatile bool     bufAuto     = true;     // subir el buffer solo si hay cortes
  volatile bool     adpcm       = true;     // pedir audio comprimido ADPCM (4x menos datos)
  volatile uint16_t pktNomMs    = 43;       // duracion de audio de un paquete SND (ms): 43 en crudo, 171 en ADPCM
  volatile uint16_t bufMs       = 0;        // contenido actual del buffer (ms)
  volatile uint16_t bufTargetMs = 0;        // objetivo actual (ms)
  volatile uint16_t underruns   = 0;        // cortes de audio desde que arranco
  volatile float    rssiDbm     = -127.0f;
  volatile bool     adcOvl      = false;
  volatile uint32_t bandwidthHz = 30000000;  // el Kiwi lo informa por MSG
  volatile bool     wifiOk      = false;
  volatile bool     sndOk       = false;
  volatile bool     apMode      = false;     // modo configuracion (WiFi propia)
  volatile uint32_t infoUntil   = 0;         // mostrar pantalla con la IP hasta este millis()
  char              status[32]  = "";
  char              toast[20]   = "";       // aviso breve en la linea superior (p. ej. cambio de banda)
  volatile uint32_t toastUntil  = 0;
  char              ip[20]      = "";
};
extern RadioState st;
