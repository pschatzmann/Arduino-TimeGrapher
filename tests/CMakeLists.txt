# Each test_*.cpp is its own executable and CTest case.
file(GLOB TTG_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

foreach(test_source ${TTG_TEST_SOURCES})
  get_filename_component(test_name ${test_source} NAME_WE)
  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} PRIVATE TinyTimeGrapher arduino_emulator)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# The rendered pages of the desktop simulator must match docs/ (see
# tools/screenshots.py; skipped without Python 3 / Pillow).
if(TTG_BUILD_EXAMPLES)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME screenshots
             COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/screenshots.py
                     --simulator $<TARGET_FILE:desktop-simulator>
                     --out ${CMAKE_BINARY_DIR}/screenshots)
    set_tests_properties(screenshots PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
  endif()
endif()
