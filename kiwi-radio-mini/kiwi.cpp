#include "kiwi.h"
#include "audio.h"
#include "config.h"
#include "settings.h"
#include "kiwiws.h"
#include "debug.h"
#include "kiwiadpcm.h"
#include <esp_system.h>
#include <stdarg.h>
#include <time.h>

static uint32_t lastKeep = 0, lastTune = 0;
static bool tuneDirty = false;
static bool active = false;
static uint8_t  attempt = 0;          // numero de intento (alterna la forma de enviar la clave)
static uint32_t discAt = 0;           // momento en que el Kiwi cerro la conexion
static bool     wantRetry = false;
static uint32_t sndCount = 0;
static bool     badpSeen = false;
static ImaAdpcm adpcmDec;
static bool     adpcmFirst = true;     // el primer paquete ADPCM solo sirve para fijar el estado del decodificador
static bool     compSent = true;       // ultimo valor de compresion enviado al Kiwi
static void startWs();

static void sendf(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  kwsSendText(buf);
}

// Clave codificada para URL (vacia = "#", que el Kiwi entiende como "sin clave")
static void passEncoded(char* out, size_t cap) {
  const char* p = currentServer().pass;
  if (!p[0]) { strlcpy(out, (attempt & 1) ? "" : "#", cap); return; }
  size_t o = 0;
  for (; *p && o + 4 < cap; p++) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = c;
    else o += snprintf(out + o, cap - o, "%%%02X", (unsigned char)c);
  }
  out[o] = 0;
}

static void sendTune() {
  if (!st.sndOk) return;
  const ModeDef& m = MODES[st.mode];
  sendf("SET mod=%s low_cut=%d high_cut=%d freq=%.3f", m.kiwi, m.lo, m.hi, st.freqHz / 1000.0);
}

void kiwiRequestTune() { tuneDirty = true; }

// ---------- recepcion ----------
static void handleMsg(const uint8_t* p, size_t len) {
  if (len <= 3) return;
  char buf[400];
  size_t n = len - 3;
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, p + 3, n);
  buf[n] = 0;
  for (size_t i = 0; i < n; i++) if ((uint8_t)buf[i] < 32 && buf[i] != 0) buf[i] = ' ';
  Serial.printf("[kiwi] MSG: %.150s\n", buf);
  const char* v;
  if ((v = strstr(buf, "bandwidth="))) st.bandwidthHz = strtoul(v + 10, nullptr, 10);
  if (strstr(buf, "too_busy")) strlcpy(st.status, "Servidor ocupado", sizeof(st.status));
  if ((v = strstr(buf, "badp="))) {
    int b = atoi(v + 5);
    if (b == 1)      strlcpy(st.status, "Clave incorrecta", sizeof(st.status));
    else if (b != 0) snprintf(st.status, sizeof(st.status), "Kiwi rechaza badp=%d", b);
    if (b != 0) badpSeen = true;
  }
  if (strstr(buf, "down=1"))   strlcpy(st.status, "Kiwi en mantenim.", sizeof(st.status));
  if (strstr(buf, "redirect")) strlcpy(st.status, "Kiwi redirige", sizeof(st.status));
}

static void handleSnd(const uint8_t* p, size_t len) {
  if (len < 3) return;
  if (!memcmp(p, "SND", 3)) {
    if (len < 10) return;
    uint8_t flags = p[3];                       // p[4..7] = secuencia (little endian, no se usa)
    uint16_t sm = (p[8] << 8) | p[9];
    st.rssiDbm = sm * 0.1f - 127.0f;
    st.adcOvl  = flags & 0x02;
    bool ad = flags & 0x10;
    st.pktNomMs = ad ? 171 : 43;
    dbgPacket(len, ad);
    const uint8_t* d = p + 10;
    size_t bytes = len - 10;

    if (ad) {                                   // ADPCM: 1 byte = 2 muestras; el estado pasa entre paquetes
      static int16_t pcm[512];
      bool skip = adpcmFirst;
      adpcmFirst = false;
      for (size_t o = 0; o < bytes; o += 256) {
        size_t nb = bytes - o < 256 ? bytes - o : 256;
        size_t ns = adpcmDec.decode(d + o, nb, pcm);
        if (!skip) audioPush(pcm, ns);
      }
      if (skip) return;
    } else {
      static int16_t pcm[512];
      size_t n = bytes / 2;
      if (n > 512) n = 512;
      bool le = flags & 0x80;
      for (size_t i = 0; i < n; i++) {
        pcm[i] = le ? (int16_t)(d[2 * i] | (d[2 * i + 1] << 8))
                    : (int16_t)((d[2 * i] << 8) | d[2 * i + 1]);
      }
      audioPush(pcm, n);
    }
    if (st.status[0]) st.status[0] = 0;
    if (sndCount++ == 0) Serial.printf("[kiwi] llega audio (SND) OK, %s\n", ad ? "ADPCM" : "PCM crudo");
  } else if (!memcmp(p, "MSG", 3)) {
    handleMsg(p, len);
  }
}

static void onWsMsg(uint8_t op, const uint8_t* d, size_t len) {
  if (op == 2) handleSnd(d, len);
  else Serial.printf("[kiwi] TEXT: %.*s\n", (int)(len > 150 ? 150 : len), (const char*)d);
}

static void onOpen() {
  st.sndOk = true;
  sndCount = 0;
  Serial.printf("[kiwi] WebSocket abierto (intento %u)\n", attempt);
  dbgReset();
  dbgConnected(kwsConnectMs, kwsHandshakeMs);
  char pw[100];
  passEncoded(pw, sizeof(pw));
  sendf("SET auth t=kiwi p=%s", pw);
  sendf("SERVER DE CLIENT %s SND", KIWI_IDENT);
  sendf("SET AR OK in=%d out=%d", KIWI_AUDIO_RATE, I2S_RATE);
  sendf("SET squelch=0 max=0");
  sendf("SET genattn=0");
  sendf("SET gen=0 mix=-1");
  compSent = st.adpcm;
  adpcmDec.reset();
  adpcmFirst = true;
  sendf("SET compression=%d", compSent ? 1 : 0);
  sendf("SET ident_user=%s", KIWI_IDENT);
  sendf("SET agc=1 hang=0 thresh=-100 slope=6 decay=1000 manGain=50");
  sendTune();
  lastKeep = millis();
}

static void onClosed() {
  Serial.printf("[kiwi] WebSocket cerrado: %s (audio recibido: %lu paquetes)\n", kwsCloseReason(), (unsigned long)sndCount);
  st.sndOk = false;
  if (!st.status[0]) strlcpy(st.status, "Kiwi cerro conexion", sizeof(st.status));
  dbgEvent("CIERRE: %s (paquetes de audio: %lu)", kwsCloseReason(), (unsigned long)sndCount);
  discAt = millis();
  wantRetry = true;
}

// ---------- API ----------
void kiwiDisconnect() {
  kwsClose();
  active = false;
  st.sndOk = false;
  st.rssiDbm = -127.0f;
  st.status[0] = 0;
  audioFlush();
}

static void startWs() {
  const KiwiServer& s = currentServer();
  char path[48];
  snprintf(path, sizeof(path), "/kiwi/%lu/SND", (unsigned long)(time(nullptr) > 100000 ? (unsigned long)time(nullptr) : (esp_random() % 2000000000UL)));
  Serial.printf("[kiwi] conectando a ws://%s:%u%s\n", s.host, (unsigned)s.port, path);
  active = true;
  wantRetry = false;
  badpSeen = false;
  char err[80];
  bool opened = kwsOpen(s.host, s.port, path, onWsMsg, err, sizeof(err));
  dbgLoopSkip();                                // la apertura bloquea unos segundos: no es un bucle lento
  if (opened) {
    onOpen();
  } else {
    dbgEvent("NO CONECTA con el Kiwi: %s (TCP %lu ms)", err, (unsigned long)kwsConnectMs);
    Serial.printf("[kiwi] no se pudo abrir: %s\n", err);
    snprintf(st.status, sizeof(st.status), "Sin conexion Kiwi");
    discAt = millis();
    wantRetry = true;
  }
}

void kiwiConnect() {
  if (cfg.serverCount == 0) { kiwiDisconnect(); return; }
  st.sndOk = false;
  st.rssiDbm = -127.0f;
  st.bandwidthHz = 30000000;
  st.status[0] = 0;
  audioFlush();
  attempt = 0;
  startWs();
}

void kiwiLoop() {
  if (!active) return;
  if (kwsIsOpen() && !kwsPoll()) onClosed();
  uint32_t now = millis();

  if (wantRetry && now - discAt >= (badpSeen ? 15000UL : 4000UL)) {      // reintento con id nuevo
    attempt++;
    startWs();
  }

  if (tuneDirty && now - lastTune >= 40) {      // limite de ritmo mientras giras
    tuneDirty = false;
    lastTune = now;
    sendTune();
  }
  if (now - lastKeep >= 1000) {                 // el Kiwi pide al menos uno por segundo
    lastKeep = now;
    if (kwsIsOpen()) sendf("SET keepalive");
  }
  if (kwsIsOpen() && st.adpcm != compSent) {    // cambio de compresion desde la web: se aplica al momento
    compSent = st.adpcm;
    adpcmDec.reset();
    adpcmFirst = true;
    sendf("SET compression=%d", compSent ? 1 : 0);
    audioFlush();
  }
}
