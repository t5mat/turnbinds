cmake_minimum_required(VERSION 3.10)

project(turnbinds CXX)

set_property(
    SOURCE
        "${CMAKE_CURRENT_LIST_DIR}/turnbinds.rc"
    APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/turnbinds.ico"
)

add_executable(turnbinds
    "${CMAKE_CURRENT_LIST_DIR}/turnbinds.rc"
    "${CMAKE_CURRENT_LIST_DIR}/turnbinds.cpp"
)

target_compile_options(turnbinds PRIVATE -std=c++17 -Wall -Werror -Wno-switch)
target_link_libraries(turnbinds PRIVATE -static -lversion -lpathcch)
set_target_properties(turnbinds PROPERTIES LINK_FLAGS_RELEASE -s)
