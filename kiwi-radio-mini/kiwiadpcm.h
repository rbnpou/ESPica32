#pragma once
// Decodificador IMA ADPCM (4 bits por muestra) como el que usa el KiwiSDR con "SET compression=1".
// Cada byte lleva dos muestras: primero el nibble bajo y luego el alto. El estado (valor previo e
// indice del paso) pasa de un paquete al siguiente, y se pone a cero al empezar.
#include <stdint.h>
#include <stddef.h>

struct ImaAdpcm {
  int32_t pred = 0;
  int32_t index = 0;

  void reset() { pred = 0; index = 0; }

  int16_t one(uint8_t code) {
    static const int16_t STEP[89] = {
      7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
      80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
      544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
      2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
      12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
    static const int8_t ADJ[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
    int32_t step = STEP[index];
    int32_t diff = step >> 3;
    if (code & 1) diff += step >> 2;
    if (code & 2) diff += step >> 1;
    if (code & 4) diff += step;
    if (code & 8) diff = -diff;
    pred += diff;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    index += ADJ[code & 7];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    return (int16_t)pred;
  }

  // n bytes -> 2n muestras
  size_t decode(const uint8_t* in, size_t n, int16_t* out) {
    for (size_t i = 0; i < n; i++) {
      out[2 * i]     = one(in[i] & 0x0F);
      out[2 * i + 1] = one(in[i] >> 4);
    }
    return 2 * n;
  }
};
