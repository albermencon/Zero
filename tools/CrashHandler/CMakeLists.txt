add_executable(ZeroCrashHandler main.cpp)

target_include_directories(ZeroCrashHandler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/Core/Crash
)

target_link_libraries(ZeroCrashHandler PRIVATE dbghelp)

set_target_properties(ZeroCrashHandler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    INTERPROCEDURAL_OPTIMIZATION OFF
)

