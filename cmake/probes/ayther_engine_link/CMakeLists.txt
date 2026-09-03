cmake_minimum_required(VERSION 3.21)
project(AytherEngineLinkProbe LANGUAGES CXX)

find_package(Ayther 0.1.0 CONFIG REQUIRED COMPONENTS engine)

add_executable(ayther_engine_link_probe main.cpp)
target_compile_features(ayther_engine_link_probe PRIVATE cxx_std_20)
target_link_libraries(ayther_engine_link_probe PRIVATE Ayther::engine)
