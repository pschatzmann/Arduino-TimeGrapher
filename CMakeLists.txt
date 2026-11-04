cmake_minimum_required(VERSION 3.14)

project(TinyTimeGrapher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TTG_BUILD_EXAMPLES "Build the desktop examples" ON)
option(TTG_BUILD_TESTS "Build unit tests" ON)

# Override any of these with -DFETCHCONTENT_SOURCE_DIR_<NAME>=/path/to/local
# to build against local checkouts instead of fetching them from GitHub.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Only our own examples/tests are wanted.
set(TMD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TGPU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Pinned: the GUI needs tinymd::Page, PageNavigator and WifiLoginPage, which
# are newer than the TinyMaterialDesign release v0.3.0.
FetchContent_Declare(
  tinymaterialdesign
  GIT_REPOSITORY https://github.com/pschatzmann/TinyMaterialDesign.git
  GIT_TAG        37afd4ad2c050b8e21024c2a5e04dbe28467582c
)
FetchContent_MakeAvailable(tinymaterialdesign)

# The audio-tools headers (and TinyGPU) need the Arduino API on the desktop.
FetchContent_Declare(
  arduino_emulator
  GIT_REPOSITORY https://github.com/pschatzmann/Arduino-Emulator.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(arduino_emulator)

if(NOT TARGET arduino-audio-tools)
  set(ADD_ARDUINO_EMULATOR OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    arduino_audio_tools
    GIT_REPOSITORY https://github.com/pschatzmann/arduino-audio-tools.git
    GIT_TAG        main
  )
  FetchContent_MakeAvailable(arduino_audio_tools)
endif()

# Header-only interface library
add_library(TinyTimeGrapher INTERFACE)
target_include_directories(TinyTimeGrapher INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(TinyTimeGrapher INTERFACE TinyMaterialDesign arduino-audio-tools)
# On the desktop the audio-tools headers only use the Arduino API (provided by
# the Arduino-Emulator) when ARDUINO is defined; IS_DESKTOP selects their
# desktop platform configuration.
target_compile_definitions(TinyTimeGrapher INTERFACE ARDUINO IS_DESKTOP)

if(TTG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(TTG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
