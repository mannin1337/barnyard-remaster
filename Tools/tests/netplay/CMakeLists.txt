cmake_minimum_required(VERSION 3.20)
project(BarnyardNetplayTests LANGUAGES CXX)

enable_testing()
set(OPENBARNYARD_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../OpenBarnyard/Source"
    CACHE PATH "OpenBarnyard engine source directory")

foreach(test IN ITEMS view_cadence darts_view_input golf_ground chicken_motion minigame_names
    input_delivery mud_local_input pool_local_shot whack_input)
    add_executable(${test} ${test}.cpp)
    target_include_directories(${test} PRIVATE "${OPENBARNYARD_SOURCE}")
    target_compile_features(${test} PRIVATE cxx_std_17)
    add_test(NAME ${test} COMMAND ${test})
endforeach()

add_executable(coop_minigame_protocol ../coop_minigame_protocol.cpp)
target_compile_features(coop_minigame_protocol PRIVATE cxx_std_17)
add_test(NAME coop_minigame_protocol COMMAND coop_minigame_protocol)

if(WIN32)
    add_executable(raw_mouse_frame raw_mouse_frame.cpp)
    target_compile_features(raw_mouse_frame PRIVATE cxx_std_17)
    target_link_libraries(raw_mouse_frame PRIVATE user32)
    add_test(NAME raw_mouse_frame COMMAND raw_mouse_frame)
endif()
