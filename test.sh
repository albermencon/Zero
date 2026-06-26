#!/bin/bash
set -e

# This script generates the project, builds the test suite, and automatically runs it.
# Usage: ./test.sh [build_dir]

BUILD_DIR=${1:-build}

echo "Configuring CMake for Tests in $BUILD_DIR..."
cmake -B "$BUILD_DIR" -DZERO_BUILD_TESTS=ON

echo ""
echo "Building and running test suite..."
cmake --build "$BUILD_DIR" --target run_tests

echo "[SUCCESS] All tests passed!"
