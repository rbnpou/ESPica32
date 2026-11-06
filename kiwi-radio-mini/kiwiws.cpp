#include "kiwiws.h"
#include <WiFi.h>
#include <esp_system.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifndef KWS_LOG
#define KWS_LOG(...) Serial.printf(__VA_ARGS__)
#endif

#define RX_MAX 4096                   // mensaje completo maximo (un paquete SND de Kiwi mide ~1 kB)

static WiFiClient cl;
static bool      isOpen = false;
static KwsMsgCb  onMsg = nullptr;
static char      reason[96] = "";
uint32_t kwsConnectMs = 0, kwsHandshakeMs = 0;

// --- estado del lector de tramas ---
enum { S_HDR, S_EXT, S_BODY };
static uint8_t  state = S_HDR;
static uint8_t  hdr[2], ext[12];
static uint8_t  hdrHave, extNeed, extHave;
static uint8_t  fOp, fFin, fMasked, mkey[4];
static uint32_t fLen, fGot;
static bool     fDiscard;

static uint8_t  rx[RX_MAX];           // mensaje de datos en construccion
static size_t   rxLen = 0;
static uint8_t  msgOp = 0;
static bool     msgDrop = false;
static uint8_t  ctl[128];             // cuerpo de tramas de control (ping/close)
static uint32_t nFrames = 0;

static void setReason(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void setReason(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(reason, sizeof(reason), fmt, ap);
  va_end(ap);
}

static void resetParser() {
  state = S_HDR; hdrHave = 0; extHave = 0; extNeed = 0;
  rxLen = 0; msgOp = 0; msgDrop = false; nFrames = 0;
}

static void hardClose() {
  cl.stop();
  isOpen = false;
}

// ---------- envio ----------
static bool sendFrame(uint8_t op, const uint8_t* p, size_t n) {
  if (!isOpen && op != 8) return false;
  uint8_t buf[320];
  if (n > 300) return false;
  size_t o = 0;
  buf[o++] = 0x80 | op;
  if (n < 126) buf[o++] = 0x80 | (uint8_t)n;
  else { buf[o++] = 0x80 | 126; buf[o++] = (uint8_t)(n >> 8); buf[o++] = (uint8_t)n; }
  uint32_t r = esp_random();
  uint8_t k[4] = { (uint8_t)r, (uint8_t)(r >> 8), (uint8_t)(r >> 16), (uint8_t)(r >> 24) };
  memcpy(buf + o, k, 4); o += 4;
  for (size_t i = 0; i < n; i++) buf[o + i] = p[i] ^ k[i & 3];
  o += n;
  return cl.write(buf, o) == o;
}

bool kwsSendText(const char* s) { return sendFrame(1, (const uint8_t*)s, strlen(s)); }

// ---------- apertura ----------
static void b64(const uint8_t* in, int n, char* out) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    uint32_t v = in[i] << 16;
    if (i + 1 < n) v |= in[i + 1] << 8;
    if (i + 2 < n) v |= in[i + 2];
    out[o++] = T[(v >> 18) & 63];
    out[o++] = T[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? T[v & 63] : '=';
  }
  out[o] = 0;
}

static void seterr(char* err, size_t cap, const char* m) { snprintf(err, cap, "%s", m); }

bool kwsOpen(const char* host, uint16_t port, const char* path, KwsMsgCb cb, char* err, size_t errCap) {
  if (isOpen) { cl.stop(); isOpen = false; }
  resetParser();
  reason[0] = 0;
  onMsg = cb;

  uint32_t tc0 = millis();
  bool okc = cl.connect(host, port, 4000);
  kwsConnectMs = millis() - tc0;
  if (!okc) { seterr(err, errCap, "no abre TCP (host/puerto/DNS)"); return false; }
  cl.setNoDelay(true);

  uint8_t rnd[16];
  for (int i = 0; i < 16; i += 4) { uint32_t r = esp_random(); memcpy(rnd + i, &r, 4); }
  char key[32];
  b64(rnd, 16, key);

  char req[360];
  int n = snprintf(req, sizeof(req),
                   "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: KiwiRadioESP32\r\n\r\n",
                   path, host, (unsigned)port, key);
  cl.write((const uint8_t*)req, n);
  uint32_t th0 = millis();

  // respuesta HTTP: se lee byte a byte hasta la linea en blanco (lo que sigue ya son tramas)
  char line[120];
  size_t ll = 0;
  uint32_t m4 = 0;
  bool gotLine = false;
  uint32_t t0 = millis();
  while (true) {
    if (millis() - t0 > 5000) { seterr(err, errCap, "el servidor no responde al handshake"); hardClose(); return false; }
    if (!cl.available()) {
      if (!cl.connected()) { seterr(err, errCap, "el servidor cerro antes del handshake"); hardClose(); return false; }
      delay(2);
      continue;
    }
    int c = cl.read();
    if (c < 0) continue;
    if (!gotLine) {
      if (c == '\r' || c == '\n') gotLine = true;
      else if (ll < sizeof(line) - 1) line[ll++] = (char)c;
    }
    m4 = (m4 << 8) | (uint8_t)c;
    if (m4 == 0x0D0A0D0A) break;
  }
  line[ll] = 0;
  if (!strstr(line, " 101")) {
    snprintf(err, errCap, "HTTP: %.60s", line);
    hardClose();
    return false;
  }
  kwsHandshakeMs = millis() - th0;
  isOpen = true;
  return true;
}

void kwsClose() {
  if (isOpen) {
    uint8_t c[2] = { 0x03, 0xE8 };            // 1000
    sendFrame(8, c, 2);
  }
  hardClose();
  setReason("cerrado por nosotros");
}

bool kwsIsOpen() { return isOpen; }
const char* kwsCloseReason() { return reason; }

// ---------- recepcion ----------
static void deliver() {
  if (onMsg && !msgDrop) onMsg(msgOp, rx, rxLen);
  rxLen = 0; msgDrop = false;
}

static void frameDone() {
  nFrames++;
  uint8_t* body = (fOp >= 8) ? ctl : rx + rxLen;
  if (fMasked && !fDiscard) for (uint32_t i = 0; i < fLen; i++) body[i] ^= mkey[i & 3];

  if (fOp == 1 || fOp == 2 || fOp == 0) {
    if (fOp != 0) { msgOp = fOp; rxLen = 0; msgDrop = false; }
    if (fDiscard) { msgDrop = true; }
    else rxLen += fLen;
    if (fFin) deliver();
  } else if (fOp == 8) {
    int code = (fLen >= 2) ? ((ctl[0] << 8) | ctl[1]) : 0;
    char txt[60] = "";
    if (fLen > 2) {
      size_t m = fLen - 2; if (m > sizeof(txt) - 1) m = sizeof(txt) - 1;
      memcpy(txt, ctl + 2, m); txt[m] = 0;
      for (size_t i = 0; txt[i]; i++) if ((uint8_t)txt[i] < 32) txt[i] = ' ';
    }
    setReason("el servidor cerro: codigo=%d '%s'", code, txt);
    uint8_t c[2] = { (uint8_t)(code >> 8), (uint8_t)code };
    sendFrame(8, c, fLen >= 2 ? 2 : 0);
    hardClose();
  } else if (fOp == 9) {
    sendFrame(10, ctl, fLen);                  // ping -> pong con el mismo contenido
  }
  // opcode 10 (pong): se ignora
}

static void beginBody() {
  fGot = 0;
  fDiscard = false;
  if (fOp >= 8) {
    if (fLen > 125) { setReason("trama de control invalida (%lu bytes)", (unsigned long)fLen); hardClose(); return; }
  } else if (rxLen + fLen > RX_MAX) {
    KWS_LOG("[ws] trama de %lu bytes demasiado grande, descartada\n", (unsigned long)fLen);
    fDiscard = true;
  }
  if (fLen == 0) { frameDone(); state = S_HDR; hdrHave = 0; return; }
  state = S_BODY;
}

bool kwsPoll() {
  if (!isOpen) return false;
  uint8_t scratch[128];
  int guard = 0;
  while (isOpen && cl.available() && guard++ < 200) {
    if (state == S_HDR) {
      int r = cl.read(hdr + hdrHave, 2 - hdrHave);
      if (r <= 0) break;
      hdrHave += r;
      if (hdrHave < 2) continue;
      fFin = hdr[0] >> 7;
      fOp = hdr[0] & 15;
      fMasked = hdr[1] >> 7;
      uint8_t l7 = hdr[1] & 127;
      extNeed = (l7 == 126 ? 2 : (l7 == 127 ? 8 : 0)) + (fMasked ? 4 : 0);
      extHave = 0;
      fLen = l7;
      if (extNeed) state = S_EXT;
      else beginBody();
    } else if (state == S_EXT) {
      int r = cl.read(ext + extHave, extNeed - extHave);
      if (r <= 0) break;
      extHave += r;
      if (extHave < extNeed) continue;
      uint8_t l7 = hdr[1] & 127;
      uint8_t o = 0;
      if (l7 == 126) { fLen = (ext[0] << 8) | ext[1]; o = 2; }
      else if (l7 == 127) {
        if (ext[0] | ext[1] | ext[2] | ext[3]) fLen = 0xFFFFFFF0u;           // enorme: se descartara
        else fLen = ((uint32_t)ext[4] << 24) | ((uint32_t)ext[5] << 16) | ((uint32_t)ext[6] << 8) | ext[7];
        o = 8;
      }
      if (fMasked) memcpy(mkey, ext + o, 4);
      beginBody();
    } else {                                                                  // S_BODY
      uint32_t want = fLen - fGot;
      uint8_t* dst;
      size_t room;
      if (fDiscard) { dst = scratch; room = sizeof(scratch); }
      else if (fOp >= 8) { dst = ctl + fGot; room = want; }
      else { dst = rx + rxLen + fGot; room = want; }
      if (room > want) room = want;
      int r = cl.read(dst, room);
      if (r <= 0) break;
      fGot += r;
      if (fGot >= fLen) {
        frameDone();
        state = S_HDR; hdrHave = 0;
      }
    }
  }
  if (isOpen && !cl.connected() && !cl.available()) {
    setReason("TCP cortado por el servidor (sin trama de cierre, %lu tramas)", (unsigned long)nFrames);
    hardClose();
  }
  return isOpen;
}
