#pragma once
#include <Arduino.h>

enum BtnEvent { BTN_NONE, BTN_SHORT, BTN_LONG, BTN_VLONG };

void     encoderInit();
int32_t  encoderDelta();     // clics acumulados desde la ultima llamada (+ sentido horario)
BtnEvent encoderButton();    // el evento se genera al SOLTAR el pulsador
