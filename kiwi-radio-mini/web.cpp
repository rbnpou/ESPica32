#include "web.h"
#include "config.h"
#include "settings.h"
#include "state.h"
#include "kiwi.h"
#include "webui.h"
#include "debug.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

static WebServer web(80);
static DNSServer dns;
static bool ap = false;
static String flash;          // mensaje que se muestra una vez en la pagina

static String esc(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&':  o += "&amp;";  break;
      case '<':  o += "&lt;";   break;
      case '>':  o += "&gt;";   break;
      case '"':  o += "&quot;"; break;
      case '\'': o += "&#39;";  break;
      default:   o += *s;
    }
  }
  return o;
}

static void redirectHome() {
  web.sendHeader("Location", "/");
  web.send(303, "text/plain", "");
}

// Admite que se pegue "http://host:8073/" en vez del host solo
static void cleanHost(char* host, size_t cap, int& port) {
  String s = host;
  s.trim();
  int i = s.indexOf("://");
  if (i >= 0) s = s.substring(i + 3);
  i = s.indexOf('/');
  if (i >= 0) s = s.substring(0, i);
  i = s.lastIndexOf(':');
  if (i >= 0) {
    int p = s.substring(i + 1).toInt();
    if (p > 0) port = p;
    s = s.substring(0, i);
  }
  strlcpy(host, s.c_str(), cap);
}

// ---------- pagina principal ----------
static void handleRoot() {
  String h;
  h.reserve(12000);
  h += F("<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>KiwiRadio</title><style>"
         "body{font-family:sans-serif;margin:12px auto;max-width:560px;padding:0 8px}"
         "input,button{font-size:16px;padding:8px;margin:3px 0}"
         "input[type=text],input[type=number],input[type=password]{width:100%;box-sizing:border-box}"
         "fieldset{margin:14px 0;border:1px solid #bbb;border-radius:6px}"
         ".row{display:flex;gap:6px;align-items:center;margin:6px 0}"
         ".n{flex:1;overflow:hidden}.n small{color:#666;display:block}"
         ".msg{background:#ffe9a8;padding:8px;border-radius:6px}"
         "#st{background:#e8eefc;padding:8px;border-radius:6px;min-height:1.2em}");
  if (!ap) h += FPSTR(CTRL_CSS);
  h += F("</style></head><body><h2>KiwiRadio</h2>");

  if (flash.length()) {
    h += F("<p class=msg>"); h += esc(flash.c_str()); h += F("</p>");
    flash = "";
  }
  if (ap) {
    h += F("<p class=msg>Modo configuraci&oacute;n: escribe tu WiFi abajo y guarda. "
           "La radio se reiniciar&aacute; y se conectar&aacute; a tu red.</p>");
  } else {
    h += F("<script>var P=[");                     // bandas: misma lista que usa el encoder
    for (int i = 0; i < BAND_COUNT; i++) {
      if (i) h += ',';
      h += F("['"); h += BANDS[i].name; h += F("',");
      h += String((unsigned long)BANDS[i].khz); h += ',';
      h += String((int)BANDS[i].mode); h += ']';
    }
    h += F("];</script>");
    h += FPSTR(CTRL_HTML);
  }

  // --- servidores guardados ---
  h += F("<fieldset><legend>Servidores guardados</legend>");
  if (!cfg.serverCount) h += F("<p>Ninguno todav&iacute;a.</p>");
  for (int i = 0; i < cfg.serverCount; i++) {
    const KiwiServer& s = cfg.servers[i];
    h += F("<div class=row><div class=n><b>"); h += esc(s.name);
    if (i == cfg.serverIdx) h += F(" &#10003;");
    h += F("</b><small>"); h += esc(s.host); h += ':'; h += String((int)s.port);
    h += F("</small></div>"
           "<form method=POST action=/use><input type=hidden name=i value=");
    h += i;
    h += F("><button>Usar</button></form>"
           "<form method=POST action=/del onsubmit=\"return confirm('Borrar?')\">"
           "<input type=hidden name=i value=");
    h += i;
    h += F("><button>X</button></form></div>");
  }
  h += F("</fieldset>");

  // --- conectar a otro Kiwi ---
  h += F("<fieldset><legend>Conectar a un Kiwi</legend>"
         "<form method=POST action=/connect>"
         "<input type=text name=host placeholder='host o IP (ej. kiwi.ejemplo.com)' required>"
         "<input type=number name=port value=8073>"
         "<input type=password name=pass placeholder='clave (si la tiene)'>"
         "<input type=text name=name placeholder='nombre (opcional)'>"
         "<button>Conectar y guardar</button></form>"
         "<p><small>M&aacute;ximo 10 servidores; con la lista llena se reemplaza el &uacute;ltimo. "
         "Puedes pegar la direcci&oacute;n completa, p. ej. http://host:8073/</small></p>"
         "<p><a href='https://kiwisdr.com/public/' target=_blank>Lista de KiwiSDR p&uacute;blicos</a></p>"
         "</fieldset>");

  // --- WiFi ---
  h += F("<fieldset><legend>WiFi</legend><details");
  if (ap) h += F(" open");
  h += F("><summary>Cambiar red WiFi</summary>"
         "<form method=POST action=/wifi>"
         "<input type=text name=ssid placeholder='Nombre de la red (SSID)' value='");
  h += esc(cfg.wifiSsid);
  h += F("'><input type=password name=wpass placeholder='Clave (vac&iacute;o = sin cambios)'>"
         "<button>Guardar y reiniciar</button></form></details></fieldset>");

  h += F("</body></html>");
  web.send(200, "text/html", h);
}

// ---------- acciones ----------
static void handleConnect() {
  char host[64];
  strlcpy(host, web.arg("host").c_str(), sizeof(host));
  int port = web.arg("port").toInt();
  cleanHost(host, sizeof(host), port);
  if (!host[0]) { flash = "Falta el host del Kiwi."; redirectHome(); return; }
  if (port <= 0 || port > 65535) port = 8073;

  String nm = web.arg("name");
  nm.trim();
  int idx = serverUpsert(nm.c_str(), host, (uint16_t)port, web.arg("pass").c_str());
  cfg.serverIdx = idx;
  settingsSave();

  if (ap) flash = "Servidor guardado. Falta la WiFi: se conectara al reiniciar.";
  else    flash = String("Conectando a ") + cfg.servers[idx].name + "...";
  redirectHome();
  if (!ap) kiwiConnect();
}

static void handleUse() {
  int i = web.arg("i").toInt();
  if (i < 0 || i >= cfg.serverCount) { redirectHome(); return; }
  cfg.serverIdx = i;
  settingsSaveIdx();
  flash = String("Conectando a ") + cfg.servers[i].name + "...";
  redirectHome();
  if (!ap) kiwiConnect();
}

static void handleDel() {
  int i = web.arg("i").toInt();
  if (i < 0 || i >= cfg.serverCount) { redirectHome(); return; }
  bool wasCurrent = serverRemove(i);
  settingsSave();
  flash = "Servidor borrado.";
  redirectHome();
  if (!ap) {
    if (cfg.serverCount == 0) kiwiDisconnect();
    else if (wasCurrent)      kiwiConnect();
  }
}

static void handleWifi() {
  String ssid = web.arg("ssid");
  ssid.trim();
  String pass = web.arg("wpass");
  if (!ssid.length()) { flash = "Falta el nombre de la WiFi."; redirectHome(); return; }
  // clave vacia + misma red = conservar la clave guardada
  if (pass.length() || strcmp(ssid.c_str(), cfg.wifiSsid) != 0)
    strlcpy(cfg.wifiPass, pass.c_str(), sizeof(cfg.wifiPass));
  strlcpy(cfg.wifiSsid, ssid.c_str(), sizeof(cfg.wifiSsid));
  settingsSave();

  web.send(200, "text/html",
           "<meta charset=utf-8><meta name=viewport content='width=device-width'>"
           "<h2>Guardado</h2><p>La radio se est&aacute; reiniciando...</p>");
  delay(800);
  ESP.restart();
}

static void handleStatus() {
  char b[160];
  uint32_t f = st.freqHz;
  const char* nm = cfg.serverCount ? cfg.servers[cfg.serverIdx].name : "-";
  snprintf(b, sizeof(b), "Kiwi: %s (%s) | %lu.%02lu kHz %s | %.0f dBm",
           nm, st.sndOk ? "conectado" : "sin conexion",
           (unsigned long)(f / 1000), (unsigned long)((f % 1000) / 10),
           MODES[st.mode].name, (double)st.rssiDbm);
  web.send(200, "text/plain", b);
}

static String jsonEsc(const char* in) {
  String o;
  for (; *in; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 32) o += ' ';
    else o += (char)c;
  }
  return o;
}

// Estado en JSON para el panel de control
static void sendState() {
  String j;
  j.reserve(240);
  const char* nm = cfg.serverCount ? cfg.servers[cfg.serverIdx].name : "-";
  j += F("{\"f\":");  j += String((unsigned long)st.freqHz);
  j += F(",\"m\":");  j += String((int)st.mode);
  j += F(",\"s\":");  j += String((int)st.stepIdx);
  j += F(",\"v\":");  j += String((int)st.volume);
  j += F(",\"ok\":"); j += String(st.sndOk ? 1 : 0);
  j += F(",\"d\":");  j += String((int)st.rssiDbm);
  j += F(",\"bf\":"); j += String((int)st.bufFloorMs);
  j += F(",\"ba\":"); j += String(st.bufAuto ? 1 : 0);
  j += F(",\"ad\":"); j += String(st.adpcm ? 1 : 0);
  j += F(",\"bm\":"); j += String((int)st.bufMs);
  j += F(",\"bt\":"); j += String((int)st.bufTargetMs);
  j += F(",\"u\":");  j += String((int)st.underruns);
  j += F(",\"n\":\""); j += jsonEsc(nm);
  j += F("\",\"st\":\""); j += jsonEsc(st.status);
  j += F("\"}");
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", j);
}

static void handleState() { sendState(); }

static void handleDebug() {
  String t;
  t.reserve(2600);
  dbgReport(t);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/plain; charset=utf-8", t);
}

// POST /api/set  con cualquiera de: f (Hz), df (Hz, relativo), m (modo), s (paso), v (volumen)
static void handleSet() {
  bool tune = false;
  if (web.hasArg("f")) {
    long f = web.arg("f").toInt();
    if (f < 10000) f = 10000;
    if (f > (long)st.bandwidthHz) f = st.bandwidthHz;
    st.freqHz = (uint32_t)f;
    tune = true;
  }
  if (web.hasArg("df")) {
    long step = STEPS[st.stepIdx];
    long f = (long)st.freqHz + web.arg("df").toInt();
    f = (f / step) * step;
    if (f < 10000) f = 10000;
    if (f > (long)st.bandwidthHz) f = st.bandwidthHz;
    st.freqHz = (uint32_t)f;
    tune = true;
  }
  if (web.hasArg("m")) {
    int m = web.arg("m").toInt();
    if (m >= 0 && m < MODE_COUNT && m != st.mode) { st.mode = m; tune = true; }
  }
  if (web.hasArg("s")) {
    int s = web.arg("s").toInt();
    if (s >= 0 && s < STEP_COUNT) st.stepIdx = s;
  }
  if (web.hasArg("bf")) {
    int b = web.arg("bf").toInt();
    if (b >= 100 && b <= 2000) st.bufFloorMs = b;
  }
  if (web.hasArg("dr")) dbgClearCounters();
  if (web.hasArg("ad")) st.adpcm = web.arg("ad").toInt() != 0;
  if (web.hasArg("ba")) st.bufAuto = web.arg("ba").toInt() != 0;
  if (web.hasArg("v")) {
    int v = web.arg("v").toInt();
    st.volume = constrain(v, 0, 100);
  }
  if (tune) kiwiRequestTune();
  sendState();
}

static void handleNotFound() {
  if (ap) handleRoot();                     // portal cautivo: cualquier direccion -> la pagina
  else    web.send(404, "text/plain", "404");
}

// ---------- API ----------
void webStart(bool apMode) {
  ap = apMode;
  if (ap) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(PORTAL_SSID, PORTAL_PASS);
    delay(300);
    dns.start(53, "*", WiFi.softAPIP());
  } else {
    if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
  }
  web.on("/", HTTP_GET, handleRoot);
  web.on("/status", HTTP_GET, handleStatus);
  if (!ap) {
    web.on("/api/state", HTTP_GET, handleState);
    web.on("/api/debug", HTTP_GET, handleDebug);
    web.on("/api/set", HTTP_POST, handleSet);
  }
  web.on("/connect", HTTP_POST, handleConnect);
  web.on("/use", HTTP_POST, handleUse);
  web.on("/del", HTTP_POST, handleDel);
  web.on("/wifi", HTTP_POST, handleWifi);
  web.onNotFound(handleNotFound);
  web.begin();
}

void webLoop() {
  if (ap) dns.processNextRequest();
  web.handleClient();
}
