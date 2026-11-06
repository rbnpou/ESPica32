#include "settings.h"
#include "state.h"
#include "config.h"
#include <Preferences.h>
#include <string.h>
#include <strings.h>

Settings cfg;
static Preferences prefs;
static const uint32_t MAGIC = 0x4B495703;   // cambiar si cambia la estructura Settings

void settingsLoad() {
  memset(&cfg, 0, sizeof(cfg));
  prefs.begin("kiwi", false);
  if (prefs.getBytesLength("cfg") == sizeof(cfg)) prefs.getBytes("cfg", &cfg, sizeof(cfg));
  uint8_t idx = prefs.getUChar("idx", 0);
  prefs.end();

  if (cfg.magic != MAGIC || cfg.serverCount > MAX_SERVERS) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = MAGIC;
  }
  cfg.serverIdx = (idx < cfg.serverCount) ? idx : 0;

  if (!cfg.wifiSsid[0]) {                       // sin WiFi guardada: usar la de config.h
    strlcpy(cfg.wifiSsid, DEFAULT_WIFI_SSID, sizeof(cfg.wifiSsid));
    strlcpy(cfg.wifiPass, DEFAULT_WIFI_PASS, sizeof(cfg.wifiPass));
  }
}

void settingsSave() {
  cfg.magic = MAGIC;
  prefs.begin("kiwi", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.putUChar("idx", cfg.serverIdx);
  prefs.end();
}

void settingsSaveIdx() {
  prefs.begin("kiwi", false);
  prefs.putUChar("idx", cfg.serverIdx);
  prefs.end();
}

bool settingsHasWifi() { return cfg.wifiSsid[0] != 0; }

const KiwiServer& currentServer() { return cfg.servers[cfg.serverIdx]; }

int serverUpsert(const char* name, const char* host, uint16_t port, const char* pass) {
  for (int i = 0; i < cfg.serverCount; i++) {
    if (!strcasecmp(cfg.servers[i].host, host) && cfg.servers[i].port == port) {
      if (name[0]) strlcpy(cfg.servers[i].name, name, sizeof(cfg.servers[i].name));
      strlcpy(cfg.servers[i].pass, pass, sizeof(cfg.servers[i].pass));
      return i;
    }
  }
  int i = (cfg.serverCount < MAX_SERVERS) ? cfg.serverCount++ : MAX_SERVERS - 1;
  KiwiServer& s = cfg.servers[i];
  memset(&s, 0, sizeof(s));
  strlcpy(s.name, name[0] ? name : host, sizeof(s.name));
  strlcpy(s.host, host, sizeof(s.host));
  s.port = port;
  strlcpy(s.pass, pass, sizeof(s.pass));
  return i;
}

bool serverRemove(int i) {
  if (i < 0 || i >= cfg.serverCount) return false;
  bool wasCurrent = (i == cfg.serverIdx);
  for (int j = i; j < cfg.serverCount - 1; j++) cfg.servers[j] = cfg.servers[j + 1];
  memset(&cfg.servers[cfg.serverCount - 1], 0, sizeof(KiwiServer));
  cfg.serverCount--;
  if (wasCurrent) cfg.serverIdx = 0;
  else if (i < cfg.serverIdx) cfg.serverIdx--;
  return wasCurrent;
}

void settingsLoadState() {
  prefs.begin("kiwi", false);
  st.freqHz = prefs.getUInt("freq", st.freqHz);
  uint8_t m = prefs.getUChar("mode", st.mode);
  if (m < MODE_COUNT) st.mode = m;
  uint8_t v = prefs.getUChar("vol", st.volume);
  st.volume = (v > 100) ? 100 : v;
  uint8_t s = prefs.getUChar("step", st.stepIdx);
  if (s < STEP_COUNT) st.stepIdx = s;
  uint16_t b = prefs.getUShort("bfloor", st.bufFloorMs);
  if (b >= 100 && b <= 2000) st.bufFloorMs = b;
  st.bufAuto = prefs.getUChar("bauto", st.bufAuto ? 1 : 0) != 0;
  st.adpcm = prefs.getUChar("adpcm", st.adpcm ? 1 : 0) != 0;
  prefs.end();
}

void settingsSaveState() {
  prefs.begin("kiwi", false);
  prefs.putUInt("freq", st.freqHz);
  prefs.putUChar("mode", st.mode);
  prefs.putUChar("vol", st.volume);
  prefs.putUChar("step", st.stepIdx);
  prefs.putUShort("bfloor", st.bufFloorMs);
  prefs.putUChar("bauto", st.bufAuto ? 1 : 0);
  prefs.putUChar("adpcm", st.adpcm ? 1 : 0);
  prefs.end();
}
