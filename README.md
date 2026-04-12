# Zero

Zero is a high-performance C++ engine framework (WIP) designed for real-time applications, featuring a modular architecture and data-oriented subsystems.

## Core Features

- **Multi-Backend RHI**: Unified Render Hardware Interface supporting **Vulkan** (1.2+) and **OpenGL** (4.6) backends.
- **Slang Shader Integration**: Cross-platform shader authoring with modern bindless rendering paths.
- **Asynchronous Job System**: Custom multi-threaded execution engine for high-throughput, low-latency task parallelization.
- **Physical Simulation**: Core integration with **JoltPhysics** for optimized collision and rigid body dynamics.
- **High-Performance Math**: SIMD-optimized math for internal kernels utilizing **Google Highway**.

## Architecture

Zero is structured as a modular set of libraries providing both low-level hardware abstraction and high-performance subsystems.

### Graphics Subsystem

The renderer is designed around a modern bindless architecture in the Vulkan backend, while maintaining a compatible legacy path for OpenGL. Shaders are compiled from **Slang**, allowing for type-safe, modular shader development.

### Job System

The engine utilizes a work-stealing job system, ensuring that heavy computations (like physics or data processing) never block the main simulation thread.

## Technology Stack

| Module | Library |
| :--- | :--- |
| **Windowing/OS** | GLFW |
| **Graphics API** | Vulkan / OpenGL (Glad) |
| **Shader compiler** | Slang |
| **Physics Service** | JoltPhysics |
| **SIMD Abstraction** | Google Highway |
| **Logging Service** | spdlog |
| **Profiling** | Tracy Profiler |

## Getting Started

To integrate Zero into your project or build the engine standalone:

### Prerequisites
- CMake 3.20+
- C++23 compliant compiler
- Vulkan SDK

### Build Instructions
```bash

git clone --recursive https://github.com/albermencon/Zero
cd Zero
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Usage

Zero is designed as a framework, not a standalone engine. It does not provide a GUI or editor.

Users are expected to:

Integrate the modules into their own application
Configure subsystems manually
Build custom runtime logic on top of the provided abstractions

### Project Status

This project is under active development. The API is unstable and subject to frequent changes.

Some modules may be incomplete, unoptimized, or non-functional.

### Design Goals

High performance through data-oriented design
Explicit control over systems and resources
Modular and extensible architecture

Originally developed as the foundation for a voxel-based project, Zero has evolved into a more general-purpose real-time framework.

### Contributing

Contributions, suggestions, and feedback are welcome.

Feel free to open issues or submit pull requests.

### Notes

I started this engine project idea thanks to the youtuber The Cherno and his engine series.