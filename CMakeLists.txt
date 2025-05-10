cmake_minimum_required(VERSION 3.28.3)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")

project(turnbinds LANGUAGES CXX)

add_executable(turnbinds)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/turnbinds.rc"
    PROPERTIES OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/turnbinds.ico"
)

file(GLOB_RECURSE src CONFIGURE_DEPENDS src/*.cpp src/*.rc)
target_sources(turnbinds PRIVATE ${src})

target_include_directories(turnbinds PRIVATE src)

set_property(TARGET turnbinds PROPERTY CXX_STANDARD 26)

target_compile_options(turnbinds PRIVATE
    -Wall -Werror
    -Wno-switch
    -Wno-vla-cxx-extension
)

target_link_libraries(turnbinds PRIVATE version pathcch)

target_link_options(turnbinds PRIVATE
    -static
    $<$<CONFIG:Release>:-Wl,--strip-all>
)

install(TARGETS turnbinds RUNTIME DESTINATION .)
