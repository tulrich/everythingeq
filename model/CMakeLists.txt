# Framework-free plugin model: parameter table and mappings, state, presets
# and undo history.  Depends on dsp/ and nlohmann_json.

set(EEQ_PRESET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../presets/factory)
file(GLOB EEQ_FACTORY_PRESETS CONFIGURE_DEPENDS ${EEQ_PRESET_DIR}/*.json)
set(EEQ_PRESET_DATA ${CMAKE_CURRENT_BINARY_DIR}/factory_presets_data.cc)
add_custom_command(
  OUTPUT ${EEQ_PRESET_DATA}
  COMMAND ${CMAKE_COMMAND} -DPRESET_DIR=${EEQ_PRESET_DIR} -DOUTPUT=${EEQ_PRESET_DATA}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed_presets.cmake
  DEPENDS ${EEQ_FACTORY_PRESETS} ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed_presets.cmake
  COMMENT "Embedding factory presets")

add_library(eeq_model STATIC
  engine_params_json.cc
  param_table.cc
  presets.cc
  response_curves.cc
  slot_mapping.cc
  spectrum_analyzer.cc
  state.cc
  undo_history.cc
  ${EEQ_PRESET_DATA}
)
target_link_libraries(eeq_model PUBLIC eeq_dsp nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(eeq_model PRIVATE /W4)
else()
  target_compile_options(eeq_model PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
