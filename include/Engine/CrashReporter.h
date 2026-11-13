#pragma once
#include <Engine/Core.h>

namespace Zero
{
    struct ZERO_API CrashReporterConfig
    {
        const char* appName        = "Zero";
        const char* dumpDir        = nullptr; // UTF-8
        const char* helperExe      = nullptr; // UTF-8
        bool        fullMemoryDump = false;
    };

    class ZERO_API CrashReporter
    {
    public:
        static void Init(const CrashReporterConfig& config = {});
        static void Shutdown();
        static void SetAppName(const char* appName);
        static void ReportFatal(const char* reason);
    };
}
