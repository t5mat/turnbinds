cmake_minimum_required(VERSION 3.10)

project(turnbinds CXX)

add_executable(turnbinds "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
target_compile_options(turnbinds PRIVATE -std=c++17 -Wall -Werror -Wno-switch)
target_link_libraries(turnbinds PRIVATE -static)
set_target_properties(turnbinds PROPERTIES LINK_FLAGS_RELEASE -s)
