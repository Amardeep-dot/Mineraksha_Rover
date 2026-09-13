#pragma once
#include <ESP_I2S.h>
#include <atomic>
#include <math.h>
#include "Config.h"

struct AudioFrame { int16_t samples[AUDIO_SAMPLES]; };
I2SClass audioBus;
QueueHandle_t speakerQueue = nullptr, microphoneQueue = nullptr;
// 0 idle, 1 listen to rover, 2 talk to rover, 3 speaker test.
std::atomic<int> audioMode{0}, speakerVolume{35}, microphonePeak{0};
std::atomic<uint32_t> lastAudioInput{0}, toneUntil{0}, micDrops{0}, speakerDrops{0}, audioErrors{0};
bool audioReady = false;

void audioWorker(void *) {
  int32_t input[AUDIO_SAMPLES * 2], output[AUDIO_SAMPLES * 2];
  AudioFrame mic{}, speaker{};
  float previous = 0, dcRemoved = 0, phase = 0;
  for (;;) {
    size_t got = 0, written = 0;
    esp_err_t err = i2s_channel_read(audioBus.rxChan(), input, sizeof(input), &got, 50);
    if (err != ESP_OK || got != sizeof(input)) { ++audioErrors; vTaskDelay(pdMS_TO_TICKS(1)); continue; }
    int peak = 0;
    for (size_t i = 0; i < AUDIO_SAMPLES; ++i) {
      // INMP441 L/R=GND: left slot, signed 24 bits left-aligned in 32 bits.
      float x = (input[2 * i] >> 16) * 4.0f;
      dcRemoved = x - previous + 0.995f * dcRemoved; previous = x;
      mic.samples[i] = (int16_t)constrain(dcRemoved, -32768.0f, 32767.0f);
      peak = max(peak, abs((int)mic.samples[i]));
    }
    microphonePeak.store(peak);
    int mode = audioMode.load();
    if (mode == 1 && xQueueSend(microphoneQueue, &mic, 0) != pdTRUE) ++micDrops;
    bool hasVoice = xQueueReceive(speakerQueue, &speaker, 0) == pdTRUE;
    bool live = millis() - lastAudioInput.load() < 300;
    float gain = speakerVolume.load() / 100.0f;
    for (size_t i = 0; i < AUDIO_SAMPLES; ++i) {
      int32_t sample = (mode == 2 && hasVoice && live) ? speaker.samples[i] : 0;
      if (mode == 3 && (int32_t)(toneUntil.load() - millis()) > 0) {
        sample = (int32_t)(6000.0f * sinf(phase));
        phase += 2.0f * PI * 660.0f / AUDIO_RATE; if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      // Duplicate into both slots, so MAX98357 channel selection is harmless.
      int32_t scaled = (int32_t)(sample * gain) * 65536;
      output[2 * i] = output[2 * i + 1] = scaled;
    }
    err = i2s_channel_write(audioBus.txChan(), output, sizeof(output), &written, 50);
    if (err != ESP_OK || written != sizeof(output)) ++audioErrors;
  }
}

bool beginAudio() {
  microphoneQueue = xQueueCreate(4, sizeof(AudioFrame));
  speakerQueue = xQueueCreate(10, sizeof(AudioFrame));
  if (!microphoneQueue || !speakerQueue) return false;
  audioBus.setPins(AUDIO_BCLK, AUDIO_WS, SPEAKER_DATA, MIC_DATA);
  // Both converters share clocks: stereo, 32-bit slots, Philips I2S.
  if (!audioBus.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO)) return false;
  if (xTaskCreatePinnedToCore(audioWorker, "rover-audio", 6144, nullptr, 2, nullptr, 1) != pdPASS) {
    audioBus.end(); return false;
  }
  return true;
}

void stopAudio() {
  audioMode.store(0);
  if (speakerQueue) xQueueReset(speakerQueue);
  if (microphoneQueue) xQueueReset(microphoneQueue);
}
