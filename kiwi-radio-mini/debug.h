#pragma once
// Diagnostico de cortes de audio: mide la llegada de paquetes del Kiwi, el estado del buffer y del
// propio ESP32 para saber SI EL CORTE VIENE DE LA RED, DEL ESP32 o DEL BUFFER/RELOJ.
#include <Arduino.h>

void dbgReset();                          // conexion nueva con el Kiwi: pone a cero las estadisticas
void dbgConnected(uint32_t tcpMs, uint32_t hsMs);   // tiempos de apertura (aprox. el ping al Kiwi)
void dbgPacket(size_t len, bool adpcm);   // llega un paquete de audio (crudo = 43 ms de audio, ADPCM = 171 ms)
void dbgDropped();                        // paquete descartado: el buffer estaba lleno
void dbgDrift(int adj);                   // ajuste de reloj: +1 repite una muestra, -1 quita una
void dbgUnderrun();                       // (tarea de audio) el buffer se vacio -> hay un corte
void dbgBufSample(uint16_t bufMs);        // (tarea de audio) nivel actual del buffer
void dbgLoopTick();                       // al principio de loop(): detecta bucles lentos
void dbgLoopSkip();                       // tras una operacion bloqueante conocida (abrir conexion)
void dbgEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void dbgPoll();                           // en loop(): clasifica los cortes y anota los huecos
void dbgReport(String& out);              // texto para la pagina web
void dbgClearCounters();
