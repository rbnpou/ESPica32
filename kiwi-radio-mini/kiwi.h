#pragma once
#include "state.h"

void kiwiConnect();        // (re)conecta al servidor actual de cfg; vale tambien para cambiar de servidor
void kiwiDisconnect();     // corta la conexion
void kiwiLoop();           // llamar en loop()
void kiwiRequestTune();    // marca que hay que enviar modo/frecuencia (con limite de ritmo)
