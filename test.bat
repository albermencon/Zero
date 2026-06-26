@echo off
setlocal

:: This script generates the project, builds the test suite, and automatically runs it.
:: Usage: test.bat [build_dir]

set BUILD_DIR=%1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

echo Configuring CMake for Tests in %BUILD_DIR%...
cmake -B %BUILD_DIR% -DZERO_BUILD_TESTS=ON

if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed!
    exit /b %errorlevel%
)

echo.
echo Building and running test suite...
cmake --build %BUILD_DIR% --target run_tests

if %errorlevel% neq 0 (
    echo [ERROR] Tests failed!
    exit /b %errorlevel%
)

echo [SUCCESS] All tests passed!
exit /b 0
