add_executable(fmfit fmfit/main.cpp)
target_link_libraries(fmfit PRIVATE eeq_dsp)

add_executable(render render/main.cpp)
target_link_libraries(render PRIVATE eeq_dsp nlohmann_json::nlohmann_json)
