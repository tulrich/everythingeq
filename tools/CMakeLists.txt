add_executable(fmfit fmfit/fmfit_main.cc)
target_link_libraries(fmfit PRIVATE eeq_dsp)

add_executable(render render/render_main.cc)
target_link_libraries(render PRIVATE eeq_dsp nlohmann_json::nlohmann_json)
