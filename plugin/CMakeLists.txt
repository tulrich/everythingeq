cmake_minimum_required(VERSION 3.24)
project(EverythingEQ VERSION 0.1.0)

# The plugin is a separate CMake project from the DSP library and tests,
# because iPlug2.cmake sets global state (C++ standard, MSVC static runtime)
# that every target linked into the plugin must share.

set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/iPlug2" CACHE PATH "iPlug2 root directory")
set(IPLUG2_CXX_STANDARD 20 CACHE STRING "C++ standard for iPlug2")
if(NOT EXISTS "${IPLUG2_DIR}/iPlug2.cmake")
  message(FATAL_ERROR "iPlug2 not found. Run scripts/setup-deps.sh first.")
endif()

# Signing must happen before installation, so this project does its own
# deployment instead of iPlug2's (see eeq_sign_and_install below).
set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Handled by EEQ_INSTALL_PLUGINS" FORCE)
option(EEQ_INSTALL_PLUGINS "Copy built plugins into the per-user plug-in folders" OFF)

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

add_subdirectory(../dsp ${CMAKE_BINARY_DIR}/dsp)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    EverythingEQ.cpp
    EverythingEQ.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS APP VST3 CLAP AU
  LINK eeq_dsp
)

# macOS: ad-hoc sign each bundle (ABOUT.md D11), then optionally install it.
if(APPLE)
  set(_plugins "au|EverythingEQ.component|Components"
               "vst3|EverythingEQ.vst3|VST3"
               "clap|EverythingEQ.clap|CLAP"
               "app|EverythingEQ.app|")
  foreach(_entry IN LISTS _plugins)
    string(REPLACE "|" ";" _entry "${_entry}")
    list(GET _entry 0 _suffix)
    list(GET _entry 1 _bundle)
    list(GET _entry 2 _folder)
    set(_target ${PROJECT_NAME}-${_suffix})
    if(NOT TARGET ${_target})
      continue()
    endif()
    set(_path "${CMAKE_BINARY_DIR}/out/${_bundle}")
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND codesign --force --sign - --timestamp=none "${_path}"
      COMMENT "Ad-hoc signing ${_bundle}")
    if(EEQ_INSTALL_PLUGINS AND _folder)
      set(_dest "$ENV{HOME}/Library/Audio/Plug-Ins/${_folder}")
      add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_dest}/${_bundle}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_path}" "${_dest}/${_bundle}"
        COMMENT "Installing ${_bundle} to ${_dest}")
    endif()
  endforeach()
endif()
