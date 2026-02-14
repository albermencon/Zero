#pragma once
#include "Core.h"
#include <memory>
#include <string_view>
#include <format>

namespace VoxelEngine
{
    // Define Log Levels
    enum class LogLevel
    {
        Trace = 0,
        Info,
        Warn,
        Error,
        Critical
    };

    class ENGINE_API Log
    {
    public:
        static void Init();

        // The single entry point.
        // We pass the final formatted string to the .cpp to keep spdlog hidden.
        static void LogMessage(bool isCore, LogLevel level, std::string_view message);

    private:
        // PImpl idiom to completely hide spdlog symbols
        struct Impl;
        static std::unique_ptr<Impl> s_Impl;
    };
}

// We use std::format (C++20/23) to format the string *before* passing it to the engine.
// This keeps the formatting type-safe and standard.

#define LOG_CALL(isCore, level, ...) \
    ::VoxelEngine::Log::LogMessage(isCore, level, std::format(__VA_ARGS__))

// Core Logging
#define ENGINE_CORE_TRACE(...) LOG_CALL(true, ::VoxelEngine::LogLevel::Trace, __VA_ARGS__)
#define ENGINE_CORE_INFO(...) LOG_CALL(true, ::VoxelEngine::LogLevel::Info, __VA_ARGS__)
#define ENGINE_CORE_WARN(...) LOG_CALL(true, ::VoxelEngine::LogLevel::Warn, __VA_ARGS__)
#define ENGINE_CORE_ERROR(...) LOG_CALL(true, ::VoxelEngine::LogLevel::Error, __VA_ARGS__)
#define ENGINE_CORE_CRITICAL(...) LOG_CALL(true, ::VoxelEngine::LogLevel::Critical, __VA_ARGS__)

// Client Logging
#define CLIENT_TRACE(...) LOG_CALL(false, ::VoxelEngine::LogLevel::Trace, __VA_ARGS__)
#define CLIENT_INFO(...) LOG_CALL(false, ::VoxelEngine::LogLevel::Info, __VA_ARGS__)
#define CLIENT_WARN(...) LOG_CALL(false, ::VoxelEngine::LogLevel::Warn, __VA_ARGS__)
#define CLIENT_ERROR(...) LOG_CALL(false, ::VoxelEngine::LogLevel::Error, __VA_ARGS__)
#define CLIENT_CRITICAL(...) LOG_CALL(false, ::VoxelEngine::LogLevel::Critical, __VA_ARGS__)
