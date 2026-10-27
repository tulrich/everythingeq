add_library(eeq_dsp STATIC
  svf/svf.cc
  bands/band_design.cc
  bands/band_processor.cc
  bands/fletcher_munson.cc
  bands/iso226.cc
  bands/vowel.cc
  dynamics/dynamics_group.cc
  dynamics/tracker.cc
  engine/engine.cc
)

# Includes are written relative to the repository root, e.g. "dsp/svf/svf.h".
target_include_directories(eeq_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(MSVC)
  target_compile_options(eeq_dsp PRIVATE /W4 /fp:fast-)
else()
  target_compile_options(eeq_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
