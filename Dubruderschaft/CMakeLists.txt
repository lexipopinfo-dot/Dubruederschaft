cmake_minimum_required(VERSION 3.25)
project(Dubruderschaft VERSION 0.3.1)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)

find_package(iPlug2 REQUIRED)

# v0.3.1: the UI is ONE flat panel PNG ("panel.png", 600x500) + the
# Patrick Hand font. They live in the repo as base64 text
# (resources/art/panel_*.json, resources/fonts/*.ttf.b64) because the
# GitHub connector can only push text files. Decode them at configure
# time, before iplug_add_plugin, so the resource files below exist
# when the target is defined. decode_assets.py is idempotent and fails
# loudly if any source is missing or corrupt.
find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_EXECUTABLE)
  message(FATAL_ERROR "Python3 is required to decode the bundled UI art (base64)")
endif()
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/decode_assets.py ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE DECODE_RESULT
  OUTPUT_VARIABLE DECODE_OUTPUT
  ERROR_VARIABLE DECODE_ERROR
)
if(NOT DECODE_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to decode UI art assets: ${DECODE_ERROR}")
endif()
message(STATUS "Decoded UI art assets:\n${DECODE_OUTPUT}")

# VST3 only for the Phase 2 smoke test: one target, fastest build, and the
# exact deliverable Lexi copies to ~/Library/Audio/Plug-Ins/VST3/.
# AU / CLAP can be added later by changing FORMATS (see README.md).
iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    Dubruderschaft.cpp
    Dubruderschaft.h
    DubruderschaftDSP.cpp
    DubruderschaftDSP.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
    resources/fonts/PatrickHand-Regular.ttf
    resources/art/panel.png
  FORMATS
    VST3
)
