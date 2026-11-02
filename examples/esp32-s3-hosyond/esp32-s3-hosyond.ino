/**
 * @file esp32-s3-hosyond.ino
 * @brief Time Grapher on the ESP32-S3 2.8" Display (FBBA0125-002 / ESP32-S3
 * Hosyond Display): ILI9341 240x320 touch display and ES8311 codec with
 * microphone.
 *
 * Hold the microphone (next to the USB-C connector) against the movement or
 * the case of the watch and press Start.
 *
 * - The audio is captured in its own task on core 0, the GUI runs in loop().
 * - The settings are stored in the flash (Preferences).
 * - "Demo mode" in the Settings measures a simulated watch instead of the
 *   microphone.
 *
 * Dependencies: TinyTimeGrapher, TinyMaterialDesign, TinyGPU,
 * arduino-audio-tools and arduino-audio-driver.
 *
 * Arduino IDE: board "ESP32S3 Dev Module", PSRAM "OPI PSRAM",
 * Flash Size "16MB".
 */
#include <Preferences.h>

#include "AudioTools.h"
#include "AudioTools/AudioLibs/AudioBoardStream.h"
#include "AudioTools/Concurrency/RTOS.h"
#include "AudioBoards/ESP32S3HosyondDisplay.h"  // arduino-audio-driver
#include "TinyTimeGrapher.h"

const int kSampleRate = 48000;
const int kChannels = 2;
const size_t kFrames = 256;

ESP32S3HosyondLCD board;  // display + touch (TinyGPU)
AudioBoardStream codec(audio_driver::ESP32S3HosyondDisplay);  // microphone
TimeGrapherEngine engine;
TimeGrapherOutput grapherOut(engine);  // feeds the engine
StreamCopy copier(grapherOut, codec, kFrames * kChannels * sizeof(int16_t));
TimeGrapherApp<RGB565> gui(board, engine);
audio_tools::Task audioTask("audio", 8192, 10, 0);
Preferences prefs;

/// Settings stored in the flash; the version invalidates old layouts.
const uint32_t kConfigVersion = 3;

GrapherConfig loadConfig(bool& dark) {
  GrapherConfig cfg;
  prefs.begin("timegrapher", true);
  if (prefs.getUInt("version", 0) == kConfigVersion && prefs.getBytesLength("config") == sizeof(cfg)) {
    prefs.getBytes("config", &cfg, sizeof(cfg));
  }
  dark = prefs.getBool("dark", true);
  prefs.end();
  return cfg;
}

void saveConfig(const GrapherConfig& cfg, bool dark) {
  prefs.begin("timegrapher", false);
  prefs.putUInt("version", kConfigVersion);
  prefs.putBytes("config", &cfg, sizeof(cfg));
  prefs.putBool("dark", dark);
  prefs.end();
  Serial.println("Settings saved");
}

/// Copies the microphone audio to the engine.
void audioLoop() {
  if (copier.copy() == 0) delay(1);
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  bool dark = true;
  GrapherConfig cfg = loadConfig(dark);
  engine.begin(kSampleRate, cfg);

  // display + touch first: the touch controller and the codec share the I2C bus
  gui.onSettingsChanged = saveConfig;
  gui.begin(dark);

  auto audioCfg = codec.defaultConfig(RX_MODE);
  audioCfg.sample_rate = kSampleRate;
  audioCfg.channels = kChannels;
  audioCfg.bits_per_sample = 16;
  audioCfg.input_device = ADC_INPUT_LINE1;
  if (!codec.begin(audioCfg)) {
    Serial.println("Audio codec could not be started");
  }
  codec.setInputVolume(0.8f);
  grapherOut.begin(codec.audioInfo());

  audioTask.begin(audioLoop);
}

void loop() { gui.update(); }
