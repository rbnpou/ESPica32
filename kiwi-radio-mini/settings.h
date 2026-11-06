#pragma once
#include <Arduino.h>

#define MAX_SERVERS 10

struct KiwiServer {
  char     name[24];
  char     host[64];
  uint16_t port;
  char     pass[32];
};

struct Settings {
  uint32_t   magic;
  char       wifiSsid[33];
  char       wifiPass[64];
  uint8_t    serverCount;
  uint8_t    serverIdx;
  KiwiServer servers[MAX_SERVERS];
};
extern Settings cfg;

void settingsLoad();                 // WiFi + servidores desde la flash
void settingsSave();                 // guarda WiFi + servidores
void settingsSaveIdx();              // guarda solo el servidor elegido
bool settingsHasWifi();
const KiwiServer& currentServer();

// Anade el servidor (o actualiza si ya existe host+puerto). Si la lista esta llena
// reemplaza el ultimo. Devuelve su indice. No guarda en flash.
int  serverUpsert(const char* name, const char* host, uint16_t port, const char* pass);
// Borra el servidor i. Devuelve true si era el servidor activo. No guarda en flash.
bool serverRemove(int i);

void settingsLoadState();            // ultima frecuencia, modo, volumen, paso
void settingsSaveState();
