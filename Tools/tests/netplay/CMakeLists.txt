cmake_minimum_required(VERSION 3.20)
project(BarnyardNetplayTests LANGUAGES CXX)

enable_testing()
set(OPENBARNYARD_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../OpenBarnyard/Source"
    CACHE PATH "OpenBarnyard engine source directory")

foreach(test IN ITEMS view_cadence darts_view_input golf_ground chicken_motion minigame_names)
    add_executable(${test} ${test}.cpp)
    target_include_directories(${test} PRIVATE "${OPENBARNYARD_SOURCE}")
    target_compile_features(${test} PRIVATE cxx_std_17)
    add_test(NAME ${test} COMMAND ${test})
endforeach()
