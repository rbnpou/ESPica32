#pragma once
// Colchon de audio (jitter buffer) del ESP32.
//  - 'floor' = minimo elegido por el usuario (ms). El objetivo nunca baja de ese valor.
//  - modo auto: tras un corte el objetivo sube 300 ms (hasta BUF_MAX_MS) y vuelve despacio al minimo.
//  - DriftCtl: mantiene el nivel del buffer estable aunque el reloj de audio del ESP32 y el del Kiwi
//    no coincidan exactamente (quita o repite 1 muestra de vez en cuando; ~0,8 % de ajuste maximo).
#include <stdint.h>
#include <stddef.h>

#define BUF_MAX_MS        2000
#define BUF_MIN_MS        100
#define BUF_AUTO_STEP_MS  300
#define BUF_BYTES_PER_MS  24          // 12000 muestras/s * 2 bytes
#define DRIFT_HI_BYTES    (150 * BUF_BYTES_PER_MS)   // por encima de objetivo+150 ms se quita una muestra

struct BufCtl {
  uint16_t autoMs = 0;
  uint32_t lastUnderrun = 0, lastDecay = 0;

  uint16_t targetMs(uint16_t floorMs, bool autoOn) const {
    if (!autoOn) return floorMs;
    return autoMs > floorMs ? autoMs : floorMs;
  }
  void onUnderrun(uint32_t now, uint16_t floorMs) {
    lastUnderrun = lastDecay = now;
    uint32_t base = autoMs > floorMs ? autoMs : floorMs;
    uint32_t v = base + BUF_AUTO_STEP_MS;
    autoMs = v > BUF_MAX_MS ? BUF_MAX_MS : (uint16_t)v;
  }
  // tras 90 s sin cortes baja 100 ms cada 30 s, sin pasar del minimo
  void tick(uint32_t now, uint16_t floorMs) {
    if (autoMs > floorMs && now - lastUnderrun > 90000UL && now - lastDecay > 30000UL) {
      autoMs = (autoMs < floorMs + 100) ? floorMs : autoMs - 100;
      lastDecay = now;
    }
  }
};

struct DriftCtl {
  float sm = 0;
  bool init = false;
  void reset(size_t avail) { sm = (float)avail; init = true; }
  // +1 = repetir una muestra (nivel bajo), -1 = quitar una (nivel alto), 0 = nada
  int decide(size_t avail, size_t target) {
    if (!init) reset(avail);
    sm += ((float)avail - sm) * 0.03f;           // media movil: ignora el diente de sierra de los paquetes
    if (sm < (float)target) return +1;
    if (sm > (float)(target + DRIFT_HI_BYTES)) return -1;
    return 0;
  }
};
