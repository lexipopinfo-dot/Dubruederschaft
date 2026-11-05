cmake_minimum_required(VERSION 3.25)
project(Dubruederschaft VERSION 0.1.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)

find_package(iPlug2 REQUIRED)

# VST3 only for the Phase 2 smoke test: one target, fastest build, and the
# exact deliverable Lexi copies to ~/Library/Audio/Plug-Ins/VST3/.
# AU / CLAP can be added later by changing FORMATS (see README.md).
iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    Dubruederschaft.cpp
    Dubruederschaft.h
    DubruederschaftDSP.cpp
    DubruederschaftDSP.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS
    VST3
)
