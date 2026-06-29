# Add doctest as an interface library
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest)

# Find all test source files automatically
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

# Define the test executable
add_executable(ZeroTests
    ${TEST_SOURCES}
)

# Link against the engine (Zero) and doctest
target_link_libraries(ZeroTests PRIVATE Zero doctest)

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

# Register the test executable with CTest
add_test(NAME ZeroTestRun COMMAND ZeroTests)

# Add a convenient target to build and execute tests in one command
add_custom_target(run_tests
    COMMAND ZeroTests
    DEPENDS ZeroTests
    COMMENT "Running Zero Engine Test Suite..."
)
