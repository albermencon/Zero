cmake_minimum_required(VERSION 3.20)

project(glad C)

add_library(glad STATIC
    src/glad.c
)

target_include_directories(glad
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Silence third-party warnings
if(MSVC)
    target_compile_options(glad PRIVATE /W0)
else()
    target_compile_options(glad PRIVATE -w)
endif()

set_target_properties(glad PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_library(glad::glad ALIAS glad)
