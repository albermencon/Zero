#include "pch.h"
#include <Engine/CrashReporter.h>

#ifndef PLATFORM_WINDOWS

#include <cstdlib>

namespace Zero
{
    void CrashReporter::Init(const CrashReporterConfig&) {}
    void CrashReporter::Shutdown() {}
    void CrashReporter::SetAppName(const char*) {}

    void CrashReporter::ReportFatal(const char*)
    {
        std::abort();
    }
}

#endif
