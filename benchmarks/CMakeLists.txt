# Default to Release mode if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type" FORCE)
endif()

# Micro-benchmark build options
option(ZERO_BENCHMARK_ENABLE_LTO "Enable Link Time Optimization (LTO) for benchmarks" ON)
option(ZERO_BENCHMARK_ENABLE_ASAN "Enable AddressSanitizer for benchmarks" OFF)
option(ZERO_BENCHMARK_ENABLE_TSAN "Enable ThreadSanitizer for benchmarks" OFF)
option(ZERO_BENCHMARK_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for benchmarks" OFF)

# Configure Google Benchmark settings
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_LTO ${ZERO_BENCHMARK_ENABLE_LTO} CACHE BOOL "" FORCE)
set(HAVE_STD_REGEX ON CACHE BOOL "" FORCE)
set(HAVE_STEADY_CLOCK ON CACHE BOOL "" FORCE)

# Add Google Benchmark as a subproject
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/benchmark
    ${CMAKE_CURRENT_BINARY_DIR}/benchmark_build
    EXCLUDE_FROM_ALL
)

# Find all benchmark source files automatically
file(GLOB_RECURSE BENCHMARK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Define the benchmark executable
add_executable(ZeroBenchmarks
    ${BENCHMARK_SOURCES}
)

# Link against the engine (Zero) and Google Benchmark
target_link_libraries(ZeroBenchmarks PRIVATE
    Zero
    benchmark::benchmark
    benchmark::benchmark_main
)

target_include_directories(ZeroBenchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/moodycamel
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# Enforce Release optimizations and NDEBUG for accurate benchmarking
if(MSVC)
    target_compile_options(ZeroBenchmarks PRIVATE /O2)
    target_compile_options(benchmark PRIVATE /O2)
    target_compile_options(benchmark_main PRIVATE /O2)
else()
    target_compile_options(ZeroBenchmarks PRIVATE -O3)
    target_compile_options(benchmark PRIVATE -O3)
    target_compile_options(benchmark_main PRIVATE -O3)
endif()

target_compile_definitions(ZeroBenchmarks PRIVATE NDEBUG)
target_compile_definitions(benchmark PRIVATE NDEBUG)
target_compile_definitions(benchmark_main PRIVATE NDEBUG)

# Link Time Optimization (LTO)
if(ZERO_BENCHMARK_ENABLE_LTO)
    set_target_properties(ZeroBenchmarks PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION ON
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
    )
endif()

# Sanitizers
if(ZERO_BENCHMARK_ENABLE_ASAN)
    target_compile_options(ZeroBenchmarks PRIVATE -fsanitize=address)
    target_link_options(ZeroBenchmarks PRIVATE -fsanitize=address)
endif()

if(ZERO_BENCHMARK_ENABLE_TSAN)
    target_compile_options(ZeroBenchmarks PRIVATE -fsanitize=thread)
    target_link_options(ZeroBenchmarks PRIVATE -fsanitize=thread)
endif()

if(ZERO_BENCHMARK_ENABLE_UBSAN)
    target_compile_options(ZeroBenchmarks PRIVATE -fsanitize=undefined)
    target_link_options(ZeroBenchmarks PRIVATE -fsanitize=undefined)
endif()

# Add a convenient target to build and execute benchmarks in one command
add_custom_target(run_benchmarks
    COMMAND ZeroBenchmarks
    DEPENDS ZeroBenchmarks
    COMMENT "Running Zero Engine Micro-Benchmarks..."
)
