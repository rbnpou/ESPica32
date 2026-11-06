#include "debug.h"
#include "state.h"
#include "settings.h"
#include <WiFi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Paquete SND de Kiwi: 512 muestras a 12 kHz = 42,7 ms -> ~23,4 paquetes/s
#define PKT_MS          42.7f
#define GAP_EVENT_MS    200     // un retraso (sobre lo normal) mayor que esto se apunta como evento
#define GAP_NET_MS      150     // al cortarse el audio: sin datos desde hace mas que esto (sobre lo normal) = red

// ---------- estadisticas de la conexion actual ----------
static volatile uint32_t lastPktMs = 0;
static uint32_t pkts, bytes, droppedPk;
static uint32_t openMs, tcpMs, hsMs, connCount;
static uint32_t gapHist[6];
static uint32_t nomMs = 43;            // intervalo normal entre paquetes
static bool     isAdpcm = false;
static uint32_t gapMax, gapMaxAt;
static volatile int32_t driftP, driftM;
static volatile uint16_t bufMin = 0xFFFF;
static uint16_t bufMinPrev = 0xFFFF;
static uint32_t bufWinT;

// ---------- bucle principal ----------
static uint32_t lastLoop, loopMaxWin, loopMaxPrev, loopWinT, loopSlow, lastSlowAt, lastSlowDur;

// ---------- cortes ----------
static volatile uint32_t uSeq, uAt, uGap;
static uint32_t seenSeq;
static uint32_t underNet, underEsp, underBuf;
static int wifiMin = 0, wifiNow = 0;
static uint32_t wifiT;

// ---------- registro de eventos ----------
#define EV_N 14
static struct { uint32_t t; char txt[84]; } ev[EV_N];
static uint8_t evHead = 0, evCount = 0;

static void fmtClock(uint32_t ms, char* b, size_t n) {
  uint32_t s = ms / 1000;
  snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}

void dbgEvent(const char* fmt, ...) {
  char txt[84];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(txt, sizeof(txt), fmt, ap);
  va_end(ap);
  ev[evHead].t = millis();
  strlcpy(ev[evHead].txt, txt, sizeof(ev[evHead].txt));
  evHead = (evHead + 1) % EV_N;
  if (evCount < EV_N) evCount++;
  Serial.printf("[dbg] %s\n", txt);
}

// ---------- entradas ----------
void dbgReset() {
  pkts = bytes = 0;
  memset(gapHist, 0, sizeof(gapHist));
  gapMax = gapMaxAt = 0;
  lastPktMs = 0;
  driftP = driftM = 0;
  droppedPk = 0;
  openMs = millis();
  bufMin = 0xFFFF; bufMinPrev = 0xFFFF; bufWinT = openMs;
  connCount++;
}

void dbgConnected(uint32_t t, uint32_t h) { tcpMs = t; hsMs = h; }

void dbgPacket(size_t len, bool ad) {
  uint32_t now = millis();
  nomMs = ad ? 171 : 43;
  isAdpcm = ad;
  pkts++;
  bytes += len;
  if (lastPktMs) {
    uint32_t gap = now - lastPktMs;
    uint32_t ex = gap > nomMs ? gap - nomMs : 0;                  // retraso sobre lo normal
    int b = ex < 20 ? 0 : ex < 80 ? 1 : ex < 200 ? 2 : ex < 450 ? 3 : ex < 950 ? 4 : 5;
    gapHist[b]++;
    if (ex > gapMax) { gapMax = ex; gapMaxAt = now; }
    if (ex > GAP_EVENT_MS) {
      bool esp = lastSlowAt >= lastPktMs && lastSlowDur > 100;
      dbgEvent("RETRASO %lu ms sin paquetes%s (buffer %u ms, WiFi %d dBm)", (unsigned long)ex,
               esp ? " [bucle ESP32 lento]" : "", (unsigned)st.bufMs, (int)WiFi.RSSI());
    }
  }
  lastPktMs = now;
}

void dbgDropped() { droppedPk++; }
void dbgDrift(int adj) { if (adj > 0) driftP++; else if (adj < 0) driftM++; }
void dbgBufSample(uint16_t ms) { if (ms < bufMin) bufMin = ms; }

void dbgUnderrun() {                       // tarea de audio: solo toma una foto; lo demas lo hace dbgPoll()
  uint32_t now = millis();
  uint32_t lp = lastPktMs;
  uGap = lp ? now - lp : 0x7FFFFFFF;
  uAt = now;
  uSeq = uSeq + 1;
}

void dbgLoopTick() {
  uint32_t now = millis();
  if (lastLoop) {
    uint32_t p = now - lastLoop;
    if (p > loopMaxWin) loopMaxWin = p;
    if (p > 100) {
      loopSlow++;
      lastSlowAt = now;
      lastSlowDur = p;
      if (p > 300) dbgEvent("BUCLE LENTO: el ESP32 tardo %lu ms en volver a atender la red", (unsigned long)p);
    }
  }
  lastLoop = now;
  if (now - loopWinT > 60000) { loopMaxPrev = loopMaxWin; loopMaxWin = 0; loopWinT = now; }
}

void dbgLoopSkip() { lastLoop = millis(); }

void dbgPoll() {
  uint32_t now = millis();
  if (now - bufWinT > 10000) { bufMinPrev = bufMin; bufMin = 0xFFFF; bufWinT = now; }
  if (now - wifiT > 2000) {
    wifiT = now;
    wifiNow = WiFi.RSSI();
    if (wifiMin == 0 || wifiNow < wifiMin) wifiMin = wifiNow;
  }
  if (uSeq != seenSeq) {
    seenSeq = uSeq;
    uint32_t gap = uGap, at = uAt;
    if (gap == 0x7FFFFFFF) {
      dbgEvent("CORTE: aun no habia audio (conexion recien abierta)");
    } else if (gap > nomMs + GAP_NET_MS) {
      bool esp = lastSlowAt >= (at - gap) && lastSlowDur > 100;
      if (esp) { underEsp++; dbgEvent("CORTE por ESP32: bucle lento (%lu ms) -> no atendio la red %lu ms", (unsigned long)lastSlowDur, (unsigned long)gap); }
      else     { underNet++; dbgEvent("CORTE por RED: %lu ms sin paquetes, WiFi %d dBm", (unsigned long)gap, (int)WiFi.RSSI()); }
    } else {
      underBuf++;
      dbgEvent("CORTE de BUFFER/RELOJ: llegaban datos (ultimo hace %lu ms) pero el buffer se vacio", (unsigned long)gap);
    }
  }
}

void dbgClearCounters() {
  underNet = underEsp = underBuf = 0;
  st.underruns = 0;
  memset(gapHist, 0, sizeof(gapHist));
  gapMax = 0;
  droppedPk = 0;
  driftP = driftM = 0;
  loopSlow = 0; loopMaxWin = loopMaxPrev = 0;
  wifiMin = 0;
  evCount = 0; evHead = 0;
  pkts = bytes = 0;
  openMs = millis();
}

// ---------- informe ----------
void dbgReport(String& out) {
  char b[200], clk[16];
  uint32_t now = millis();
  float secs = (now - openMs) / 1000.0f;

  fmtClock(now - openMs, clk, sizeof(clk));
  snprintf(b, sizeof(b), "Conexion: %s  (conexiones desde el arranque: %lu)\n", st.sndOk ? clk : "SIN CONEXION", (unsigned long)connCount);
  out += b;
  snprintf(b, sizeof(b), "Ping aprox. al Kiwi: TCP %lu ms, handshake %lu ms\n", (unsigned long)tcpMs, (unsigned long)hsMs);
  out += b;
  float pps = secs > 1 ? pkts / secs : 0;
  float need = 1000.0f / nomMs;                                  // paquetes/s necesarios para ir en tiempo real
  float thr = secs > 1 ? bytes / secs / 1024.0f : 0;
  snprintf(b, sizeof(b), "Audio: %s. Paquetes: %lu (%.1f/s; para tiempo real hacen falta %.1f/s)\n", isAdpcm ? "ADPCM comprimido" : "PCM sin comprimir",
           (unsigned long)pkts, pps, need);
  out += b;
  snprintf(b, sizeof(b), "Caudal recibido: %.1f kB/s (%.0f %% del necesario)\n", thr, need > 0 ? pps / need * 100.0f : 0.0f);
  out += b;
  snprintf(b, sizeof(b), "Retraso de cada paquete sobre lo normal (ms): <20: %lu | 20-80: %lu | 80-200: %lu | 200-450: %lu | 450-950: %lu | >950: %lu\n",
           (unsigned long)gapHist[0], (unsigned long)gapHist[1], (unsigned long)gapHist[2],
           (unsigned long)gapHist[3], (unsigned long)gapHist[4], (unsigned long)gapHist[5]);
  out += b;
  if (gapMax) {
    snprintf(b, sizeof(b), "Retraso maximo: %lu ms (hace %lu s)\n", (unsigned long)gapMax, (unsigned long)((now - gapMaxAt) / 1000));
    out += b;
  }
  uint16_t bm = bufMin < bufMinPrev ? bufMin : bufMinPrev;
  snprintf(b, sizeof(b), "Buffer: %u ms (objetivo %u ms, minimo ultimos 20 s: %u ms)\n", (unsigned)st.bufMs, (unsigned)st.bufTargetMs,
           bm == 0xFFFF ? 0u : (unsigned)bm);
  out += b;
  uint32_t total = underNet + underEsp + underBuf;
  snprintf(b, sizeof(b), "CORTES de audio: %u  ->  red %lu | ESP32 %lu | buffer/reloj %lu\n", (unsigned)st.underruns,
           (unsigned long)underNet, (unsigned long)underEsp, (unsigned long)underBuf);
  out += b;
  snprintf(b, sizeof(b), "Paquetes descartados (buffer lleno): %lu | ajuste de reloj: +%ld -%ld\n", (unsigned long)droppedPk, (long)driftP, (long)driftM);
  out += b;
  uint32_t lm = loopMaxWin > loopMaxPrev ? loopMaxWin : loopMaxPrev;
  snprintf(b, sizeof(b), "WiFi: %d dBm (peor %d) | bucle del ESP32: max %lu ms, lentos >100 ms: %lu\n", wifiNow, wifiMin, (unsigned long)lm, (unsigned long)loopSlow);
  out += b;

  // veredicto
  out += "\nVeredicto: ";
  bool slow = secs > 30 && pps < need * 0.9f;
  if (slow && !isAdpcm)
    out += "la conexion NO llega a tiempo real (recibes menos audio del que se reproduce). Activa 'Audio comprimido ADPCM'.";
  else if (slow && isAdpcm)
    out += "ni con ADPCM llega a tiempo real: el Kiwi o su internet van lentos. Prueba otro Kiwi.";
  else if (st.underruns == 0 && total == 0)  out += "sin cortes.";
  else if (underNet >= underEsp && underNet >= underBuf && underNet)
    out += "los cortes vienen de la RED (no llegan paquetes a tiempo). Sube el buffer minimo, acerca el ESP32 al router o prueba otro Kiwi.";
  else if (underEsp >= underBuf && underEsp)
    out += "los cortes los causa el propio ESP32 (bucle lento). Cierra la web mientras escuchas; si sigue, avisame.";
  else if (underBuf)
    out += "llegan datos pero el buffer se vacia: sube el buffer minimo o activa 'Subir solo si hay cortes'.";
  else out += "cortes sin clasificar.";
  out += "\n";

  out += "\nUltimos eventos (el mas reciente arriba):\n";
  if (!evCount) out += "  (ninguno)\n";
  for (int i = 0; i < evCount; i++) {
    int k = (evHead - 1 - i + EV_N) % EV_N;
    fmtClock(ev[k].t, clk, sizeof(clk));
    snprintf(b, sizeof(b), "  %s  %s\n", clk, ev[k].txt);
    out += b;
  }
}
