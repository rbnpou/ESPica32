#pragma once
#include <Arduino.h>

void audioInit();
// Mete muestras PCM de 16 bits (12 kHz, mono) en el buffer. Descarta el paquete si no caben.
void audioPush(const int16_t* samples, size_t n);
// Vacia el buffer (al cambiar de servidor)
void audioFlush();
