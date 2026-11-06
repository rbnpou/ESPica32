#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "state.h"
#include "settings.h"
#include "audio.h"
#include "kiwi.h"
#include "display.h"
#include "encoder.h"
#include "web.h"
#include "debug.h"

static uint32_t focusT = 0;      // ultimo uso del encoder en volumen/modo

// Un solo encoder:
//  - girar         -> actua sobre la funcion activa (frecuencia / volumen / modo)
//  - clic          -> cambia de funcion (frecuencia -> volumen -> modo)
//  - doble clic    -> siguiente banda (160 m, 80 m, 40 m, WWV, 30 m ... 10 m)
//  - clic largo    -> cambia el paso de sintonia
//  - clic muy largo-> muestra la direccion de la web en la pantalla
static void handleEncoder() {
  uint32_t now = millis();
  int32_t d = encoderDelta();
  BtnEvent b = encoderButton();

  static uint32_t lastShort = 0;
  static uint8_t  focusBefore = F_FREQ;
  if (b == BTN_SHORT) {
    if (lastShort && now - lastShort < 450) {          // doble clic: deshace el cambio de funcion y salta de banda
      st.focus = focusBefore;
      lastShort = 0;
      int bi = bandIndexFor(st.freqHz) + 1;
      if (bi >= BAND_COUNT) bi = 0;
      st.freqHz = BANDS[bi].khz * 1000UL;
      st.mode = BANDS[bi].mode;
      snprintf(st.toast, sizeof(st.toast), "Banda %s", BANDS[bi].name);
      st.toastUntil = now + 1500;
      kiwiRequestTune();
    } else {
      focusBefore = st.focus;
      st.focus = (st.focus + 1) % FOCUS_COUNT;
      lastShort = now;
    }
    focusT = now;
  } else if (b == BTN_LONG) {
    st.stepIdx = (st.stepIdx + 1) % STEP_COUNT;
    st.focus = F_FREQ;
  } else if (b == BTN_VLONG) {
    st.infoUntil = now + 8000;
  }

  if (d) {
    focusT = now;
    switch (st.focus) {
      case F_FREQ: {
        static uint32_t lastMove = 0;
        uint32_t dt = now - lastMove;
        lastMove = now;
        int mult = (abs(d) > 1 || dt < 30) ? 10 : (dt < 70 ? 4 : 1);   // giro rapido = salto mayor
        int64_t step = STEPS[st.stepIdx];
        int64_t f = (int64_t)st.freqHz + (int64_t)d * step * mult;
        f = (f / step) * step;
        if (f < 10000) f = 10000;
        if (f > (int64_t)st.bandwidthHz) f = st.bandwidthHz;
        st.freqHz = (uint32_t)f;
        kiwiRequestTune();
        break;
      }
      case F_VOL: {
        int v = (int)st.volume + d * 2;
        st.volume = constrain(v, 0, 100);
        break;
      }
      case F_MODE: {
        int m = (int)st.mode + (d > 0 ? 1 : -1);
        st.mode = (m + MODE_COUNT) % MODE_COUNT;
        kiwiRequestTune();
        break;
      }
    }
  }

  // tras 4 s sin tocar, el encoder vuelve a sintonizar
  if (st.focus != F_FREQ && now - focusT > 4000) st.focus = F_FREQ;
}

// Guarda frecuencia/modo/volumen 3 s despues del ultimo cambio (menos desgaste de la flash)
static void autosaveState() {
  static uint32_t snapF = 0;
  static uint8_t snapM = 255, snapV = 255, snapS = 255, snapA = 255, snapC = 255;
  static uint16_t snapB = 0;
  static bool dirty = false;
  static uint32_t tChange = 0;
  uint32_t now = millis();

  if (st.freqHz != snapF || st.mode != snapM || st.volume != snapV || st.stepIdx != snapS || st.bufFloorMs != snapB || (uint8_t)st.bufAuto != snapA || (uint8_t)st.adpcm != snapC) {
    snapF = st.freqHz; snapM = st.mode; snapV = st.volume; snapS = st.stepIdx; snapB = st.bufFloorMs; snapA = st.bufAuto; snapC = st.adpcm;
    dirty = true;
    tChange = now;
  }
  if (dirty && now - tChange > 3000) {
    settingsSaveState();
    dirty = false;
  }
}

void setup() {
  Serial.begin(115200);
  settingsLoad();
  settingsLoadState();
  encoderInit();
  displayInit();
  audioInit();
  strlcpy(st.status, "Iniciando...", sizeof(st.status));
  delay(100);

  // Configuracion: sin WiFi guardada, o encoder pulsado al encender
  bool needSetup = (digitalRead(PIN_ENC_SW) == LOW) || !settingsHasWifi();

  if (!needSetup) {
    strlcpy(st.status, "Conectando WiFi...", sizeof(st.status));
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_NAME);
    WiFi.setSleep(false);                // sin ahorro: menos jitter en el audio
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(true);
      needSetup = true;                  // la red guardada no responde: abrir configuracion
    }
  }

  st.status[0] = 0;
  if (needSetup) {
    st.apMode = true;
    webStart(true);                      // el resto ocurre en loop()
    return;
  }

  st.wifiOk = true;
  strlcpy(st.ip, WiFi.localIP().toString().c_str(), sizeof(st.ip));
  webStart(false);
  st.infoUntil = millis() + 7000;        // muestra la direccion de la web al arrancar
  if (cfg.serverCount) kiwiConnect();
}

void loop() {
  dbgLoopTick();
  webLoop();
  if (st.apMode) { delay(2); return; }

  st.wifiOk = (WiFi.status() == WL_CONNECTED);
  kiwiLoop();
  dbgPoll();
  handleEncoder();
  autosaveState();
  delay(1);
}
