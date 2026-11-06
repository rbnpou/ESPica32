#pragma once
// Cliente WebSocket minimo (sin TLS) para el audio del KiwiSDR.
// Sustituye a la biblioteca WebSockets: es mas pequeno y dice POR QUE se cierra la conexion.
#include <Arduino.h>

typedef void (*KwsMsgCb)(uint8_t opcode, const uint8_t* data, size_t len);   // opcode 1 = texto, 2 = binario

// Abre TCP + handshake HTTP. true = conectado. Si falla, 'err' lleva el motivo.
bool kwsOpen(const char* host, uint16_t port, const char* path, KwsMsgCb cb, char* err, size_t errCap);

// Llamar muy a menudo. Procesa lo recibido. Devuelve false cuando la conexion termino
// (el motivo queda en kwsCloseReason()).
bool kwsPoll();

bool kwsSendText(const char* s);
void kwsClose();                       // cierre limpio iniciado por nosotros
bool kwsIsOpen();
extern uint32_t kwsConnectMs, kwsHandshakeMs;   // duracion de la ultima apertura (aprox. el ping)
const char* kwsCloseReason();
