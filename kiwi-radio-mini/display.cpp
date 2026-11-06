#include "display.h"
#include "config.h"
#include "state.h"
#include "settings.h"
#include <U8g2lib.h>

#if OLED_SH1106
static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);
#endif

static void centerStr(int y, const char* s) {
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
}

// Celda de la fila inferior; invertida = es la funcion que controla el encoder
static void cell(int x, int w, const char* txt, bool inv) {
  if (inv) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(x, 43, w, 11);
    u8g2.setDrawColor(0);
  }
  u8g2.drawStr(x + (w - u8g2.getStrWidth(txt)) / 2, 52, txt);
  u8g2.setDrawColor(1);
}

static const char* topText() {
  if ((int32_t)(st.toastUntil - millis()) > 0) return st.toast;
  if (st.status[0])        return st.status;
  if (!st.wifiOk)          return "Sin WiFi";
  if (cfg.serverCount == 0) return "Elige Kiwi en la web";
  if (!st.sndOk)           return "Conectando...";
  return cfg.servers[cfg.serverIdx].name;
}

// ---------- pantallas ----------
static void drawAp() {
  u8g2.setFont(u8g2_font_6x10_tr);
  centerStr(10, "MODO CONFIGURACION");
  centerStr(24, "1) Conecta a la WiFi:");
  centerStr(36, PORTAL_SSID);
  centerStr(48, "clave: " PORTAL_PASS);
  centerStr(61, "2) Abre 192.168.4.1");
}

static void drawInfo() {
  u8g2.setFont(u8g2_font_6x10_tr);
  centerStr(10, "CONFIG WEB");
  centerStr(26, "Abre en el navegador:");
  centerStr(40, st.ip);
  u8g2.setFont(u8g2_font_5x8_tr);
  centerStr(54, "o kiwiradio.local");
}

static void drawMain() {
  char b[32];
  float dbm = st.rssiDbm;
  bool on = st.sndOk;

  // --- linea superior: servidor/estado a la izquierda, S-meter en texto a la derecha ---
  u8g2.setFont(u8g2_font_6x10_tr);
  char sm[12] = "";
  if (on) {
    if (dbm <= -73.0f) {
      float s = (dbm + 127.0f) / 6.0f;
      snprintf(sm, sizeof(sm), "S%d", s < 0 ? 0 : (int)s);
    } else {
      snprintf(sm, sizeof(sm), "S9+%d", (int)(dbm + 73.0f));
    }
  }
  int smW = u8g2.getStrWidth(sm);
  char top[32];
  strlcpy(top, topText(), sizeof(top));
  size_t len = strlen(top);
  while (len && u8g2.getStrWidth(top) > 128 - smW - 6) top[--len] = 0;
  u8g2.drawStr(0, 9, top);
  u8g2.drawStr(128 - smW, 9, sm);

  // --- frecuencia en kHz ---
  uint32_t f = st.freqHz;
  snprintf(b, sizeof(b), "%lu.%02lu", (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 10));
  u8g2.setFont(u8g2_font_logisoso20_tn);
  centerStr(38, b);

  // --- fila inferior: modo | paso | volumen ---
  u8g2.setFont(u8g2_font_6x10_tr);
  cell(0, 36, MODES[st.mode].name, st.focus == F_MODE);

  uint32_t step = STEPS[st.stepIdx];
  if (step >= 1000) snprintf(b, sizeof(b), "%lukHz", (unsigned long)(step / 1000));
  else              snprintf(b, sizeof(b), "%luHz", (unsigned long)step);
  cell(40, 48, b, st.focus == F_FREQ);

  snprintf(b, sizeof(b), "V%d", (int)st.volume);
  cell(92, 36, b, st.focus == F_VOL);

  // --- S-meter grafico ---
  u8g2.drawFrame(0, 56, 128, 8);
  if (on) {
    float fr = (dbm + 127.0f) / 114.0f;               // S0 .. S9+60
    if (fr < 0) fr = 0;
    if (fr > 1) fr = 1;
    u8g2.drawBox(1, 58, (int)(fr * 126), 4);
  }
}

// ---------- tarea ----------
static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

// Huella de lo que se dibuja: solo se redibuja si cambia (menos trafico I2C)
static uint32_t signature(bool info) {
  uint32_t h = 2166136261u;
  h = mix(h, st.freqHz);
  h = mix(h, st.mode);
  h = mix(h, st.stepIdx);
  h = mix(h, st.volume);
  h = mix(h, st.focus);
  h = mix(h, (uint32_t)(int32_t)st.rssiDbm);
  h = mix(h, st.sndOk);
  h = mix(h, st.wifiOk);
  h = mix(h, st.apMode);
  h = mix(h, info);
  h = mix(h, cfg.serverCount);
  h = mix(h, cfg.serverIdx);
  for (const char* c = st.status; *c; c++) h = mix(h, (uint8_t)*c);
  bool toastOn = (int32_t)(st.toastUntil - millis()) > 0;
  h = mix(h, toastOn);
  if (toastOn) for (const char* c = st.toast; *c; c++) h = mix(h, (uint8_t)*c);
  for (const char* c = cfg.servers[cfg.serverIdx].name; *c; c++) h = mix(h, (uint8_t)*c);
  return h;
}

static void displayTask(void*) {
  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.setFontMode(1);                       // texto transparente

  uint32_t last = 0;
  bool first = true;
  for (;;) {
    bool info = (int32_t)(st.infoUntil - millis()) > 0;
    uint32_t sig = signature(info);
    if (first || sig != last) {
      u8g2.clearBuffer();
      if (st.apMode)  drawAp();
      else if (info)  drawInfo();
      else            drawMain();
      u8g2.sendBuffer();
      last = sig;
      first = false;
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

void displayInit() {
  xTaskCreatePinnedToCore(displayTask, "oled", 4096, nullptr, 1, nullptr, 0);
}
