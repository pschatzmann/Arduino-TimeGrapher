# TinyTimeGrapher

A header-only Arduino library that turns a microcontroller with a microphone and a touch display into a timegrapher for mechanical watches and clocks. It listens to the ticks, shows the key figures live and charts them over time.

This is the Arduino port of [Android-TimeGrapher](https://github.com/pschatzmann/Android-TimeGrapher). It uses [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) for the audio and its filters, and [TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign) for the GUI.

![Screenshots](docs/screenshots.png)

## Features

- **KPIs:** rate (s/day), beat error, amplitude, beat rate, tick/tock ratio, jitter and SNR.
- **Charts:** the standard timegrapher chart, a rolling volume chart with the detection threshold, and KPI trends (1 min / 5 min / 30 min / session).
- **Settings:** beat rate (auto or 14400 to 36000 bph), lift angle, analysis window, band-pass filter, automatic or manual gain, sensitivity, calibration offset, light/dark theme.
- **Header-only and layered:** use only the measurement engine (any audio source, no display), or the complete touch GUI.
- **Demo mode:** a switch in the Settings measures a simulated watch (`WatchSimulator`) instead of the microphone, so the timegrapher can be tried without a watch.

## Example: ESP32-S3 2.8" Display (Hosyond)

[examples/esp32-s3-hosyond](examples/esp32-s3-hosyond/esp32-s3-hosyond.ino) runs on the [ESP32-S3 2.8" Display (FBBA0125-002 / ESP32-S3 Hosyond Display)](https://github.com/pschatzmann/TinyGPU/wiki/Predefined-Boards#esp32-s3-28-display-fbba0125-002--esp32-s3-hosyond-display). It has an ILI9341 240x320 touch display and an ES8311 codec with a microphone.

- The audio is copied from the codec to the engine (`StreamCopy` into a `TimeGrapherOutput`) in its own task on core 0. The GUI runs in `loop()`.
- The settings are stored in the flash with `Preferences`.
- Uncomment `#define DEMO_MODE` to measure a simulated watch instead of the microphone.

Arduino IDE settings: board "ESP32S3 Dev Module", PSRAM "OPI PSRAM", Flash Size "16MB".

Hold the microphone against the movement or the case and press **Start**. The KPIs appear after about 8 beats.

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

## Important limits

The rate is measured against the clock of the audio codec. Use the calibration offset to compensate for it against a reference watch. The amplitude needs both sounds of a beat (unlock and drop) to be resolved and depends on the correct lift angle.

## License

[MIT](LICENSE)
