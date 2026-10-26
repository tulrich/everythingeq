add_executable(eeq_tests
  svf_test.cc
  bands_test.cc
  routing_test.cc
  dynamics_test.cc
  lookahead_test.cc
)
target_link_libraries(eeq_tests PRIVATE eeq_dsp Catch2::Catch2WithMain)

include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(eeq_tests)

# Golden-file regression renders of the use cases (ABOUT.md section 2).
# Regenerate after an intentional change with tools/render/update_goldens.sh.
if(EEQ_BUILD_TOOLS)
  file(GLOB EEQ_USECASES ${PROJECT_SOURCE_DIR}/tools/render/usecases/*.json)
  foreach(cfg ${EEQ_USECASES})
    get_filename_component(name ${cfg} NAME_WE)
    add_test(NAME usecase_${name}
             COMMAND render ${cfg} --golden ${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.csv --check)
  endforeach()
endif()
