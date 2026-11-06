#include "audio.h"
#include "config.h"
#include "state.h"
#include "audiobuf.h"
#include "debug.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include <math.h>
#include <string.h>

static StreamBufferHandle_t rb;
static volatile bool flushReq = false;
static BufCtl ctl;

#if AUDIO_INTERNAL_DAC
// El DAC interno espera muestras SIN signo (0x8000 = silencio) y solo usa el byte alto.
static inline int16_t pack(int32_t v) { return (int16_t)(uint16_t)(v + 32768); }
static const int16_t IDLE = (int16_t)-32768;   // patron 0x8000
#else
static inline int16_t pack(int32_t v) { return (int16_t)v; }
static const int16_t IDLE = 0;
#endif

void audioPush(const int16_t* s, size_t n) {
  size_t bytes = n * 2;
  if (xStreamBufferSpacesAvailable(rb) >= bytes) {
    xStreamBufferSend(rb, s, bytes, 0);
  } else {
    dbgDropped();
  }
  // si no cabe, se descarta el paquete entero (mantiene alineadas las muestras)
}

void audioFlush() { flushReq = true; }   // lo ejecuta la tarea de audio

static void audioTask(void*) {
  int16_t in[128];
  int16_t out[(128 + 1) * 4 * 2];
  int16_t sil[128];                       // 64 tramas estereo de silencio
  for (int i = 0; i < 128; i++) sil[i] = IDLE;
  int16_t prev = 0;
  bool playing = false;
  uint16_t lastT = 0;
  DriftCtl drift;
  size_t w;

  for (;;) {
    uint16_t floorMs = st.bufFloorMs;
    if (floorMs < BUF_MIN_MS) floorMs = BUF_MIN_MS;
    if (floorMs > BUF_MAX_MS) floorMs = BUF_MAX_MS;
    ctl.tick(millis(), floorMs);
    uint16_t tMs = ctl.targetMs(floorMs, st.bufAuto);
    size_t target = (size_t)tMs * BUF_BYTES_PER_MS;
    size_t avail = xStreamBufferBytesAvailable(rb);
    st.bufTargetMs = tMs;
    st.bufMs = avail / BUF_BYTES_PER_MS;
    if (playing) dbgBufSample(st.bufMs);
    if (tMs > lastT) playing = false;            // se pidio mas colchon: rellenar de nuevo
    lastT = tMs;

    if (avail > (AUDIO_RING_BYTES / 10) * 9) {   // casi lleno (rafaga tras una pausa): se salta al objetivo
      size_t drop = avail - target;
      while (drop > 0) {
        size_t r = xStreamBufferReceive(rb, in, drop < sizeof(in) ? drop : sizeof(in), 0);
        if (!r) break;
        drop -= r;
      }
      avail = xStreamBufferBytesAvailable(rb);
    }

    if (flushReq) {
      while (xStreamBufferReceive(rb, in, sizeof(in), 0) > 0) {}
      playing = false;
      prev = 0;
      flushReq = false;
      continue;
    }
    if (!playing) {
      if (avail >= target) {
        playing = true;
        drift.reset(avail);
      } else {
        // sin datos: se escribe silencio (la propia escritura marca el ritmo)
        i2s_write(I2S_NUM_0, sil, sizeof(sil), &w, portMAX_DELAY);
        continue;
      }
    }
    size_t got = xStreamBufferReceive(rb, in, sizeof(in), pdMS_TO_TICKS(30));
    size_t n = got / 2;
    if (n == 0) {                                // underrun: se vacio el buffer -> volver a rellenar
      playing = false;
      st.underruns = st.underruns + 1;
      dbgUnderrun();
      ctl.onUnderrun(millis(), floorMs);
      continue;
    }

    // compensacion de deriva: quitar o repetir una muestra mantiene el nivel del buffer estable
    // el nivel oscila (diente de sierra) tanto como dura un paquete: se centra el control a media oscilacion
    int adj = (n >= 32) ? drift.decide(avail, target + (size_t)(st.pktNomMs / 2) * BUF_BYTES_PER_MS) : 0;
    if (adj) dbgDrift(adj);
    if (adj < 0) n--;
    size_t total = n + (adj > 0 ? 1 : 0);

    float g = powf(st.volume / 100.0f, 2.0f);    // curva perceptual
    int o = 0;
    for (size_t i = 0; i < total; i++) {
      int16_t x = in[i < n ? i : n - 1];         // la muestra repetida es la ultima
      for (int k = 1; k <= 4; k++) {             // 12 kHz -> 48 kHz, lineal
        int32_t s = prev + ((int32_t)(x - prev) * k) / 4;
        int16_t v = pack((int32_t)(s * g));
        out[o++] = v;                            // L
        out[o++] = v;                            // R
      }
      prev = x;
    }
    i2s_write(I2S_NUM_0, out, o * sizeof(int16_t), &w, portMAX_DELAY);
  }
}

void audioInit() {
  rb = xStreamBufferCreate(AUDIO_RING_BYTES, 1);

  i2s_config_t ic;
  memset(&ic, 0, sizeof(ic));
  ic.sample_rate          = I2S_RATE;
  ic.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  ic.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  ic.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  ic.dma_buf_count        = 8;
  ic.dma_buf_len          = 256;
  ic.use_apll             = false;
  ic.tx_desc_auto_clear   = true;

#if AUDIO_INTERNAL_DAC
  ic.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  ic.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2s_driver_install(I2S_NUM_0, &ic, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, nullptr);                    // habilita GPIO25/26
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
#else
  ic.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  ic.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_driver_install(I2S_NUM_0, &ic, 0, nullptr);

  i2s_pin_config_t pins;
  memset(&pins, 0, sizeof(pins));
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = PIN_I2S_BCK;
  pins.ws_io_num    = PIN_I2S_LRCK;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pins);
#endif

  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 5, nullptr, 1);
}
