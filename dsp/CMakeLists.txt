add_library(eeq_dsp STATIC
  svf/Svf.cpp
  bands/BandDesign.cpp
  bands/BandProcessor.cpp
  bands/FletcherMunson.cpp
  bands/Iso226.cpp
  bands/Vowel.cpp
  dynamics/DynamicsGroup.cpp
  engine/Engine.cpp
)

target_include_directories(eeq_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(eeq_dsp PRIVATE /W4 /fp:fast-)
else()
  target_compile_options(eeq_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
