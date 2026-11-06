#include "encoder.h"
#include "config.h"
#include "driver/gpio.h"

// ---------- encoder en cuadratura (por interrupcion) ----------
static volatile uint8_t encState = 0;
static volatile int8_t  encAcc = 0;
static volatile int32_t encDelta = 0;

// En DRAM (no en flash) para que la interrupcion funcione aunque se escriba en la flash
static const int8_t DRAM_ATTR TBL[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR encIsr() {
  uint8_t s = ((encState << 2) |
               (gpio_get_level((gpio_num_t)PIN_ENC_A) << 1) |
                gpio_get_level((gpio_num_t)PIN_ENC_B)) & 0x0F;
  encState = s;
  encAcc += TBL[s];
  if (encAcc >= ENC_STEPS_PER_DETENT)       { encDelta++; encAcc = 0; }
  else if (encAcc <= -ENC_STEPS_PER_DETENT) { encDelta--; encAcc = 0; }
}

void encoderInit() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(PIN_ENC_A, encIsr, CHANGE);
  attachInterrupt(PIN_ENC_B, encIsr, CHANGE);
}

int32_t encoderDelta() {
  noInterrupts();
  int32_t d = encDelta;
  encDelta = 0;
  interrupts();
  return d;
}

// ---------- pulsador ----------
BtnEvent encoderButton() {
  static bool down = false;
  static uint32_t t0 = 0;
  bool d = digitalRead(PIN_ENC_SW) == LOW;
  uint32_t now = millis();

  if (d && !down) { down = true; t0 = now; return BTN_NONE; }
  if (!d && down) {
    down = false;
    uint32_t dt = now - t0;
    if (dt < 30) return BTN_NONE;               // anti-rebote
    if (dt < LONG_PRESS_MS) return BTN_SHORT;
    if (dt < VLONG_PRESS_MS) return BTN_LONG;
    return BTN_VLONG;
  }
  return BTN_NONE;
}
