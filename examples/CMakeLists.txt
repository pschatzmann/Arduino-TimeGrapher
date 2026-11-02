# Only the desktop simulator can be built here: esp32-s3-hosyond needs the
# real board (ES8311 codec, I2S).
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

# The simulator records with audio-tools' MiniAudioStream, which needs the
# single header miniaudio.h: downloaded once, or taken from MINIAUDIO_DIR.
set(MINIAUDIO_VERSION "0.11.22")
set(MINIAUDIO_DIR "${CMAKE_BINARY_DIR}/miniaudio" CACHE PATH "Directory containing miniaudio.h")
if(NOT EXISTS "${MINIAUDIO_DIR}/miniaudio.h")
  file(DOWNLOAD
       https://raw.githubusercontent.com/mackron/miniaudio/${MINIAUDIO_VERSION}/miniaudio.h
       "${MINIAUDIO_DIR}/miniaudio.h"
       STATUS MINIAUDIO_STATUS)
  list(GET MINIAUDIO_STATUS 0 MINIAUDIO_RC)
  if(NOT MINIAUDIO_RC EQUAL 0)
    message(FATAL_ERROR "Downloading miniaudio.h failed: ${MINIAUDIO_STATUS}")
  endif()
endif()

arduino_sketch(desktop-simulator desktop-simulator/desktop-simulator.ino
               LIBRARIES TinyTimeGrapher SDL2::SDL2 Threads::Threads ${CMAKE_DL_LIBS} m)
target_include_directories(desktop-simulator PRIVATE "${MINIAUDIO_DIR}")
target_compile_definitions(desktop-simulator PRIVATE TINYGPU_AUTO_NAMESPACE TINYMD_AUTO_NAMESPACE)
