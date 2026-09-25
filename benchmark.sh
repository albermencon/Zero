#!/bin/bash
set -e

# This script generates the project, builds the benchmark suite, and automatically runs it.
# Usage: ./benchmark.sh [build_dir]

BUILD_DIR=${1:-build}

echo "Configuring CMake for Benchmarks in $BUILD_DIR..."
cmake -B "$BUILD_DIR" -DZERO_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Building and running benchmark suite..."
cmake --build "$BUILD_DIR" --config Release --target run_benchmarks

echo "[SUCCESS] Benchmarks finished!"
