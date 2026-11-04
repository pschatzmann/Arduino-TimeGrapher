/**
 * @file esp32-s3-hosyond.ino
 * @brief Time Grapher on the ESP32-S3 2.8" Display (FBBA0125-002 / ESP32-S3
 * Hosyond Display): ILI9341 240x320 touch display and ES8311 codec with
 * microphone.
 *
 * Hold the microphone (next to the USB-C connector) against the movement or
 * the case of the watch and select Start in the menu (top right).
 *
 * - The audio is captured in its own task on core 0, the GUI runs in loop().
 * - The settings are stored in the flash (Preferences).
 * - "Demo mode" in the Settings measures a simulated watch instead of the
 *   microphone.
 * - WiFi (optional): select a network and enter the password with "WiFi" in
 *   the menu; the credentials are stored in the flash. While connected, the
 *   audio clock is compared with an NTP server (TinyNTP) to correct the
 *   measured rate; the result is stored in the flash. After about one hour
 *   it is precise to 1 ppm (0.09 s/day).
 * - "Shut down" in the menu switches off the display and puts the ESP32 into
 *   deep sleep; press the reset button to start again.
 *
 * Dependencies: TinyTimeGrapher, TinyMaterialDesign, TinyGPU,
 * arduino-audio-tools, arduino-audio-driver and TinyNTP.
 *
 * Arduino IDE: board "ESP32S3 Dev Module", PSRAM "OPI PSRAM",
 * Flash Size "16MB", Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" (the
 * sketch does not fit into the 1.2 MB of the default partition scheme).
 */
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "AudioTools.h"
#include "AudioTools/AudioLibs/AudioBoardStream.h"
#include "AudioTools/Concurrency/RTOS.h"
#include "AudioBoards/ESP32S3HosyondDisplay.h"  // arduino-audio-driver
#include "TinyNTP.h"
#include "TinyTimeGrapher.h"
#include "TinyMaterialDesign/Wifi/WifiEsp32.h"
#include "TinyTimeGrapher/NTPClockSync.h"

const int kSampleRate = 48000;
const int kChannels = 1;  // the ES8311 has one microphone (I2S left slot)
const size_t kFrames = 256;

ESP32S3HosyondLCD board;  // display + touch (TinyGPU)
AudioBoardStream codec(audio_driver::ESP32S3HosyondDisplay);  // microphone
TimeGrapherEngine engine;
TimeGrapherOutput grapherOut(engine);  // feeds the engine
StreamCopy copier(grapherOut, codec, kFrames * kChannels * sizeof(int16_t));
TimeGrapherApp<RGB565> gui(board, engine);
Esp32Wifi wifi;  // WiFi page in the menu: scan, login, stored credentials
audio_tools::Task audioTask("audio", 8192, 10, 0);
Preferences prefs;
ClockCalibrator clockCalibrator;
NTPClockSync<WiFiUDP> clockSync(engine, clockCalibrator);
audio_tools::Task ntpTask("ntp", 6144, 1, 0);
float storedClockUncertainty = 1e9f;

/// Settings stored in the flash; the version invalidates old layouts.
const uint32_t kConfigVersion = 4;

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

/// Clock correction of an earlier NTP measurement.
void loadClock() {
  Preferences p;
  p.begin("timegrapher", true);
  if (p.isKey("clockPpm")) {
    storedClockUncertainty = p.getFloat("clockUnc", 1e9f);
    clockCalibrator.setStored(p.getFloat("clockPpm", 0.0f), storedClockUncertainty);
    clockSync.apply();
  }
  p.end();
}

/// Stores the clock correction when the measurement has become more precise.
void saveClock() {
  float ppm, uncertainty;
  bool measured = false;
  if (!clockCalibrator.best(ppm, uncertainty, &measured) || !measured) return;
  if (uncertainty > storedClockUncertainty * 0.8f) return;
  Preferences p;
  p.begin("timegrapher", false);
  p.putFloat("clockPpm", ppm);
  p.putFloat("clockUnc", uncertainty);
  p.end();
  storedClockUncertainty = uncertainty;
  Serial.printf("Clock correction saved: %+.2f ppm (+-%.2f)\n", ppm, uncertainty);
}

/// Compares the audio clock with NTP (blocks while waiting for the server).
void ntpLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    return;
  }
  if (clockSync.update()) saveClock();
  delay(100);
}

/// Cleared on shutdown: the audio task stops reading from the codec.
volatile bool audioActive = true;

/// Switches off the backlight and the codec, then sleeps until reset.
void shutdown() {
  Serial.println("Shutting down");
  audioActive = false;
  delay(50);  // lets the audio task finish its current copy
  audioTask.end();
  ntpTask.end();
  WiFi.disconnect(true);
  codec.end();
  const gpio_num_t backlight = static_cast<gpio_num_t>(board.backlightPin());
  digitalWrite(backlight, LOW);
  gpio_hold_en(backlight);  // keeps the backlight off during the deep sleep
  gpio_deep_sleep_hold_en();
  Serial.flush();
  esp_deep_sleep_start();  // no wake-up source: only the reset button
}

/// Copies the microphone audio to the engine.
void audioLoop() {
  if (!audioActive || copier.copy() == 0) delay(1);
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  bool dark = true;
  GrapherConfig cfg = loadConfig(dark);
  engine.begin(kSampleRate, cfg);

  gpio_hold_dis(static_cast<gpio_num_t>(board.backlightPin()));  // set by shutdown()

  // display + touch first: the touch controller and the codec share the I2C bus
  gui.onSettingsChanged = saveConfig;
  gui.onShutdown = shutdown;
  wifi.begin();       // connects with the stored credentials (if any)
  gui.setWifi(wifi);  // "WiFi" in the menu
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

  loadClock();
  gui.setClockCalibrator(clockCalibrator);
  ntpTask.begin(ntpLoop);  // waits until the WiFi is connected
}

void loop() { gui.update(); }
