/**
 * @file desktop-simulator.ino
 * @brief The Time Grapher GUI in a desktop SDL2 window (mouse = touch),
 * measuring the default microphone of the computer via audio-tools'
 * MiniAudioStream. "Demo mode" in the Settings measures a simulated watch
 * instead.
 *
 * With the environment variable TTG_SCREENSHOTS set to a directory, no audio
 * device is opened: 90 s of a simulated watch (21600 bph, +12 s/day, 0.8 ms
 * beat error) are processed at once and every page is saved there as a BMP
 * image (TinyMaterialDesign's App::saveScreenshot()).
 */
#include <stdio.h>
#include <stdlib.h>

#include <TinyTimeGrapher.h>

#include "AudioTools/AudioLibs/MiniAudioStream.h"

const int kSampleRate = 48000;
const int kChannels = 1;

MiniAudioStream mic;
TimeGrapherEngine engine;
TimeGrapherOutput grapherOut(engine);  // feeds the engine
StreamCopy copier(grapherOut, mic);
TimeGrapherApp<RGB565> gui(DefaultBoard, engine);

void savePage(const char* dir, const char* name) {
  // let the GUI pick up the data of the page
  for (int i = 0; i < 3; ++i) {
    gui.update();
    delay(120);
  }
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
  FileStream file(path);
  if (gui.app().saveScreenshot(file)) printf("saved %s\n", path);
}

void screenshots(const char* dir) {
  WatchSimulator& watch = engine.demoWatch();
  watch.bph = 21600;
  watch.rateSPerDay = 12.0;
  watch.beatErrorMs = 0.8;
  watch.lockDropMs = 9.0;
  GrapherConfig cfg = engine.config();
  cfg.demo = true;
  engine.setConfig(cfg);
  engine.start();
  // in demo mode the engine replaces the (silent) input by the simulated watch
  static int16_t silence[1024] = {0};
  for (int i = 0; i < 90 * kSampleRate / 1024; ++i) engine.write(silence, 1024);

  const char* names[] = {"live", "graph", "trends", "settings"};
  for (int page = 0; page < 4; ++page) {
    gui.showPage(page);
    savePage(dir, names[page]);
  }
  gui.setDark(false);
  gui.showPage(0);
  savePage(dir, "live-light");
  exit(0);
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  GrapherConfig cfg;
  engine.begin(kSampleRate, cfg);
  gui.onShutdown = []() {
    mic.end();
    exit(0);
  };
  gui.begin();

  const char* dir = getenv("TTG_SCREENSHOTS");
  if (dir != nullptr) screenshots(dir);

  auto micCfg = mic.defaultConfig(RX_MODE);
  micCfg.sample_rate = kSampleRate;
  micCfg.channels = kChannels;
  micCfg.bits_per_sample = 16;
  if (!mic.begin(micCfg)) Serial.println("Microphone could not be opened");
  grapherOut.begin(mic.audioInfo());
}

void loop() {
  copier.copy();
  gui.update();
}
