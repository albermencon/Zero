# Find all regular test source files (crash subprocess tests live in Crash/)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*/Crash/.*\\.cpp$")
list(APPEND TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/TestMath.cpp")
list(REMOVE_DUPLICATES TEST_SOURCES)

# Define the test executable
add_executable(ZeroTests
    ${TEST_SOURCES}
)

# Link against the engine (Zero) and doctest
target_link_libraries(ZeroTests PRIVATE Zero doctest)
target_include_directories(ZeroTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/moodycamel
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# Separate executable for out-of-process crash capture tests
add_executable(ZeroCrashTests
    ${CMAKE_CURRENT_SOURCE_DIR}/Crash/TestCrashReporter.cpp
)
target_link_libraries(ZeroCrashTests PRIVATE Zero doctest)
if(WIN32)
    target_link_libraries(ZeroCrashTests PRIVATE dbghelp)
endif()
if(WIN32 AND TARGET ZeroCrashHandler)
    add_dependencies(ZeroCrashTests ZeroCrashHandler)
    add_custom_command(TARGET ZeroCrashTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ZeroCrashHandler>"
            "$<TARGET_FILE_DIR:ZeroCrashTests>/ZeroCrashHandler.exe"
        COMMENT "Deploying ZeroCrashHandler for ZeroCrashTests"
    )
endif()

# Add sanitizer options if requested via CMake options
option(ZERO_ENABLE_ASAN "Enable AddressSanitizer for tests" OFF)
option(ZERO_ENABLE_TSAN "Enable ThreadSanitizer for tests" OFF)
option(ZERO_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for tests" OFF)

if(ZERO_ENABLE_ASAN)
    target_compile_options(ZeroTests PRIVATE -fsanitize=address)
    target_link_options(ZeroTests PRIVATE -fsanitize=address)
endif()

if(ZERO_ENABLE_TSAN)
    target_compile_options(ZeroTests PRIVATE -fsanitize=thread)
    target_link_options(ZeroTests PRIVATE -fsanitize=thread)
endif()

if(ZERO_ENABLE_UBSAN)
    target_compile_options(ZeroTests PRIVATE -fsanitize=undefined)
    target_link_options(ZeroTests PRIVATE -fsanitize=undefined)
endif()

# Register the test executables with CTest
add_test(NAME ZeroTestRun COMMAND ZeroTests)
add_test(NAME ZeroCrashTestRun COMMAND ZeroCrashTests)

# Add convenient targets to build and execute tests in one command
add_custom_target(run_tests
    COMMAND ZeroTests
    DEPENDS ZeroTests
    COMMENT "Running Zero Engine Test Suite..."
)

add_custom_target(run_crash_tests
    COMMAND ZeroCrashTests
    DEPENDS ZeroCrashTests
    COMMENT "Running Zero Crash Reporter Test Suite..."
)
