# TinyTimeGrapher

A header-only Arduino library that turns a microcontroller with a microphone and a touch display into a timegrapher for mechanical watches and clocks. It listens to the ticks, shows the key figures live and charts them over time.

This is the Arduino port of [Android-TimeGrapher](https://github.com/pschatzmann/Android-TimeGrapher). It uses [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) for the audio and its filters, and [TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign) for the GUI.

![Screenshots](docs/screenshots.png)

## Features

- **KPIs:** rate (s/day), beat error, amplitude, beat rate, tick/tock ratio, jitter and SNR.
- **Charts:** the standard timegrapher chart, a rolling volume chart with the detection threshold, and KPI trends (1 min / 5 min / 30 min / session).
- **Settings:** beat rate (auto or 14400 to 36000 bph), lift angle, analysis window, band-pass filter, automatic or manual gain, sensitivity, calibration offset, light/dark theme.
- **Clock correction with NTP:** the audio clock is compared with an NTP server ([TinyNTP](https://github.com/pschatzmann/TinyNTP)), and the measured rate is corrected with the result (see [Accuracy of the rate](#accuracy-of-the-rate)).
- **Header-only and layered:** use only the measurement engine (any audio source, no display), or the complete touch GUI.
- **Menu:** Start / Stop the measurement and Shut down (the sketch decides what shutting down means, via `onShutdown`).
- **Demo mode:** a switch in the Settings measures a simulated watch (`WatchSimulator`) instead of the microphone, so the timegrapher can be tried without a watch.

## Example: ESP32-S3 2.8" Display (Hosyond)

[examples/esp32-s3-hosyond](examples/esp32-s3-hosyond/esp32-s3-hosyond.ino) runs on the [ESP32-S3 2.8" Display (FBBA0125-002 / ESP32-S3 Hosyond Display)](https://github.com/pschatzmann/TinyGPU/wiki/Predefined-Boards#esp32-s3-28-display-fbba0125-002--esp32-s3-hosyond-display). It has an ILI9341 240x320 touch display and an ES8311 codec with a microphone.

- The audio is copied from the codec to the engine (`StreamCopy` into a `TimeGrapherOutput`) in its own task on core 0. The GUI runs in `loop()`.
- The settings are stored in the flash with `Preferences`.
- Enter your WiFi in `kWifiSsid` and `kWifiPassword` to switch on the clock correction with NTP. The measured correction is stored in the flash, so it also applies after a restart without WiFi. Its state is shown in the Settings below the calibration offset.
- **Shut down** in the menu switches off the display and the codec and puts the ESP32 into deep sleep. Press the reset button to start again.

Arduino IDE settings: board "ESP32S3 Dev Module", PSRAM "OPI PSRAM", Flash Size "16MB".

Hold the microphone against the movement or the case and select **Start** in the menu (top right). The KPIs appear after about 8 beats.

## Usage

```c++
#include "TinyTimeGrapher.h"

TimeGrapherEngine engine;
TimeGrapherApp<RGB565> gui(board, engine);   // board: any TinyGPU LCDBoard

void setup() {
  engine.begin(48000);       // sample rate of the audio
  gui.begin();
}

void loop() {
  // audio (e.g. from a separate task): interleaved 16 bit PCM
  engine.write(pcm, frames, channels);
  gui.update();
}
```

The building blocks can also be used on their own:

| Class | Purpose |
|---|---|
| `TimeGrapherEngine` | Thread-safe measurement engine: feed it samples (`int16_t` PCM or `float`), read the ticks, KPIs, volume trace and KPI history. |
| `TimeGrapherOutput` | arduino-audio-tools `AudioOutput` that feeds the engine, e.g. `StreamCopy copier(grapherOut, mic);` |
| `TickDetector` | Band-pass (audio-tools `HighPassFilterDF1` / `LowPassFilterDF1`), rectifier, adaptive threshold, automatic gain control and lock-to-drop measurement. |
| `KpiCalculator` | Rate, beat error, jitter, tick/tock ratio and amplitude from the ticks of the analysis window. |
| `ClockCalibrator` | Measures the error of the audio clock in ppm from pairs of reference time and audio time (linear fit with uncertainty and outlier rejection). |
| `NTPClockSync<UDP>` | Feeds a `ClockCalibrator` with the time of an NTP server (TinyNTP) and applies the result to the engine. Include `TinyNTP.h` and `TinyTimeGrapher/NTPClockSync.h`: it is not part of `TinyTimeGrapher.h`. |
| `WatchSimulator` | Generates the sound of a watch with a given beat rate, rate error, beat error and lock-to-drop time. |
| `TimeGrapherApp` | The complete TinyMaterialDesign GUI. |

The signal processing uses `float` by default, because the ESP32-S3 has no double-precision FPU. Define `TTG_DSP_DOUBLE` to use `double`. Buffer sizes can be changed in [TinyTimeGrapherConfig.h](src/TinyTimeGrapher/TinyTimeGrapherConfig.h).

## Desktop build

The unit tests and a desktop version build with CMake. The desktop version shows the GUI in an SDL2 window, uses the mouse as the touch input, and records from the computer's default microphone with audio-tools' `MiniAudioStream`. "Demo mode" in the Settings works there too.

```sh
cmake -B build -S .
cmake --build build
./build/tests/test_dsp
./build/examples/desktop-simulator
```

To build against local checkouts, pass `-DFETCHCONTENT_SOURCE_DIR_<NAME>=/path` for `TINYMATERIALDESIGN`, `TINYGPU`, `ARDUINO_EMULATOR` and `ARDUINO_AUDIO_TOOLS`. The single header `miniaudio.h` (0.11.22) is downloaded at configure time; pass `-DMINIAUDIO_DIR=/path` to use a local copy. With `TTG_SCREENSHOTS=<dir>`, no audio device is opened: the desktop version measures a simulated watch, saves every page as a BMP file and exits.

## Dependencies

- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
- [TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign), which needs [TinyGPU](https://github.com/pschatzmann/TinyGPU)
- [arduino-audio-driver](https://github.com/pschatzmann/arduino-audio-driver) for the codec of the example board
- [TinyNTP](https://github.com/pschatzmann/TinyNTP) for the clock correction (optional, only needed with `NTPClockSync`). Note that TinyNTP is licensed under the GPL v3.

## Accuracy of the rate

The beats are timed by counting audio samples, so the rate is measured against the clock of the audio codec. An error of this clock of 1 ppm gives 0.0864 s/day. Crystals are typically within 10 to 50 ppm, and the actual sample rate can also differ from the nominal one because of the clock dividers. Without a correction, this means an error of several seconds per day.

`NTPClockSync` compares the audio time (`TimeGrapherEngine::audioTimeSec()`) with the time of an NTP server every 30 s. Answers with a round trip of more than 60 ms are ignored. A `ClockCalibrator` fits a line through the differences: its slope is the clock error. The uncertainty of the network time (a few ms) is averaged out over time, so precision grows with the duration:

| Duration | Typical uncertainty (5 ms network jitter) |
|---|---|
| 5 min | about 20 ppm (1.7 s/day) |
| 15 min | about 4 ppm (0.3 s/day) |
| 1 hour | below 1 ppm (0.06 s/day) |

The correction is applied once it is precise to 1 ppm, and the example stores it in the flash. Measure once for about an hour with WiFi: after that the stored correction is used, also without WiFi, and refined whenever a new measurement is more precise.

```c++
#include <WiFi.h>
#include <WiFiUdp.h>
#include "TinyNTP.h"
#include "TinyTimeGrapher.h"
#include "TinyTimeGrapher/NTPClockSync.h"

ClockCalibrator calibrator;
NTPClockSync<WiFiUDP> clockSync(engine, calibrator);

// in its own task: update() blocks until the server answers
void ntpLoop() {
  if (WiFi.status() == WL_CONNECTED) clockSync.update();
  delay(100);
}

// optional: show the state in the Settings
gui.setClockCalibrator(calibrator);
```

The correction is not applied in the demo mode. The calibration offset in the Settings is added on top: use it to compensate for a remaining difference against a reference watch.

## Important limits

The amplitude needs both sounds of a beat (unlock and drop) to be resolved and depends on the correct lift angle.

## License

[MIT](LICENSE)
