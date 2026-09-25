@echo off
setlocal

:: This script generates the project, builds the benchmark suite, and automatically runs it.
:: Usage: benchmark.bat [build_dir]

set BUILD_DIR=%1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

echo Configuring CMake for Benchmarks in %BUILD_DIR%...
cmake -B %BUILD_DIR% -DZERO_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release

if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed!
    exit /b %errorlevel%
)

echo.
echo Building and running benchmark suite...
cmake --build %BUILD_DIR% --config Release --target run_benchmarks

if %errorlevel% neq 0 (
    echo [ERROR] Benchmarks failed!
    exit /b %errorlevel%
)

echo [SUCCESS] Benchmarks finished!
exit /b 0
