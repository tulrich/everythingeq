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
# Experimental: an AUv3 app extension embedded in the standalone app
# (macOS). It registers as a separate component ('EvE3') so it can sit
# beside the AUv2.
option(EEQ_AUV3 "Build the AUv3 app extension (macOS, experimental)" OFF)

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(../dsp ${CMAKE_BINARY_DIR}/dsp)
add_subdirectory(../model ${CMAKE_BINARY_DIR}/model)

set(_formats APP VST3 CLAP AU)
if(EEQ_AUV3)
  list(APPEND _formats AUV3)
endif()

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    everything_eq.cc
    everything_eq.h
    ui/band_ribbon.h
    ui/choice_field.h
    ui/detector_scope.h
    ui/editor.cc
    ui/editor.h
    ui/editor_model.h
    ui/eq_graph.h
    ui/fader.h
    ui/level_meter.h
    ui/param_field.h
    ui/preset_bar.h
    ui/routing_map.h
    ui/theme.h
    ui/widgets.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS ${_formats}
  LINK eeq_model
)

# macOS: ad-hoc sign each bundle (ABOUT.md D11), then optionally install it.
if(APPLE)
  # The AUv3's framework and app extension go inside the app. iPlug2's own
  # embedding step only runs when the app relinks, so this target refreshes
  # them on every build, then signs inside out (the extension must be
  # sandboxed to load).
  if(EEQ_AUV3 AND TARGET ${PROJECT_NAME}-app)
    set(_app "${CMAKE_BINARY_DIR}/out/EverythingEQ.app")
    set(_fw "${_app}/Contents/Frameworks/EverythingEQAU.framework")
    set(_appex "${_app}/Contents/PlugIns/EverythingEQAUv3.appex")
    add_custom_target(${PROJECT_NAME}-auv3-embed ALL
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_fw}" "${_appex}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_app}/Contents/Frameworks"
        "${_app}/Contents/PlugIns"
      COMMAND cp -R "${CMAKE_BINARY_DIR}/out/EverythingEQAU.framework" "${_fw}"
      COMMAND cp -R "${CMAKE_BINARY_DIR}/out/EverythingEQAUv3.appex" "${_appex}"
      COMMAND codesign --force --sign - --timestamp=none "${_fw}"
      COMMAND codesign --force --sign - --timestamp=none
        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/resources/EverythingEQ-AUv3.entitlements"
        "${_appex}"
      COMMAND codesign --force --sign - --timestamp=none "${_app}"
      COMMENT "Embedding and signing the AUv3"
      VERBATIM)
    add_dependencies(${PROJECT_NAME}-auv3-embed ${PROJECT_NAME}-app
      ${PROJECT_NAME}AUv3-appex ${PROJECT_NAME}AU-framework)
  endif()

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
