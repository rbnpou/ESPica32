#include "state.h"

RadioState st;

const ModeDef MODES[MODE_COUNT] = {
  {"LSB", "lsb",  -2700,  -300},
  {"USB", "usb",    300,  2700},
  {"AM",  "am",   -4500,  4500},
  {"CW",  "cw",     300,   700},
  {"FM",  "nbfm", -6000,  6000},
};

const uint32_t STEPS[STEP_COUNT] = {10, 100, 1000, 5000, 10000};

const BandDef BANDS[BAND_COUNT] = {
  {"160m",  1840, MODE_LSB},
  {"80m",   3700, MODE_LSB},
  {"40m",   7100, MODE_LSB},
  {"WWV",  10000, MODE_AM},
  {"30m",  10100, MODE_USB},
  {"20m",  14200, MODE_USB},
  {"17m",  18110, MODE_USB},
  {"15m",  21200, MODE_USB},
  {"12m",  24930, MODE_USB},
  {"10m",  28500, MODE_USB},
};


int bandIndexFor(uint32_t hz) {
  int idx = -1;
  for (int i = 0; i < BAND_COUNT; i++) if (hz >= BANDS[i].khz * 1000UL) idx = i;
  return idx;
}
