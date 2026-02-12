#include "pch.h"
#include <Engine/Core.h>
#include <Engine/Log.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace VoxelEngine
{
    struct Log::Impl
    {
        std::shared_ptr<spdlog::logger> CoreLogger;
        std::shared_ptr<spdlog::logger> ClientLogger;
    };

    // Static instance definition
    std::unique_ptr<Log::Impl> Log::s_Impl;

    void Log::Init()
    {
        // Don't initialize twice
        if (s_Impl)
            return;

        s_Impl = std::make_unique<Impl>();

        // Create sinks (destinations)
        std::vector<spdlog::sink_ptr> logSinks;

        // 1. Console Sink (Colored)
        logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        logSinks[0]->set_pattern("%^[%T] %n: %v%$");

        // 2. File Sink
        logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("VoxelEngine.log", true));
        logSinks[1]->set_pattern("[%T] [%l] %n: %v");

        // Initialize Core Logger
        s_Impl->CoreLogger = std::make_shared<spdlog::logger>("ENGINE", begin(logSinks), end(logSinks));
        spdlog::register_logger(s_Impl->CoreLogger);
        s_Impl->CoreLogger->set_level(spdlog::level::trace);
        s_Impl->CoreLogger->flush_on(spdlog::level::trace);

        // Initialize Client Logger
        s_Impl->ClientLogger = std::make_shared<spdlog::logger>("APP", begin(logSinks), end(logSinks));
        spdlog::register_logger(s_Impl->ClientLogger);
        s_Impl->ClientLogger->set_level(spdlog::level::trace);
        s_Impl->ClientLogger->flush_on(spdlog::level::trace);
    }

    void Log::LogMessage(bool isCore, LogLevel level, std::string_view message)
    {
        if (!s_Impl)
        {
            // If not initialized, log to stderr
            std::cerr << "[UNINITIALIZED LOG] " << message << std::endl;
            return;
        }

        auto &logger = isCore ? s_Impl->CoreLogger : s_Impl->ClientLogger;

        switch (level)
        {
        case LogLevel::Trace:
            logger->trace(message);
            break;
        case LogLevel::Info:
            logger->info(message);
            break;
        case LogLevel::Warn:
            logger->warn(message);
            break;
        case LogLevel::Error:
            logger->error(message);
            break;
        case LogLevel::Critical:
            logger->critical(message);
            break;
        }
    }
}
