#pragma once

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>

namespace Zero::CrashInternal
{
    inline constexpr uint32_t kCrashMagic               = 0x5A435248u; // 'ZCRH'
    inline constexpr uint32_t kCrashVersion             = 1u;
    inline constexpr DWORD    kFatalReportExceptionCode = 0xE05A4552u; // 'ZER' fatal abort

    enum CrashFlags : uint32_t
    {
        CrashFlag_None                    = 0,
        CrashFlag_FullMemoryDump          = 1u << 0,
        CrashFlag_HasExceptionPointers    = 1u << 1,
        CrashFlag_HasExceptionRecord      = 1u << 2,
        CrashFlag_HasContextRecord        = 1u << 3,
    };

    struct CrashSharedBlock
    {
        uint32_t              magic;
        uint32_t              version;
        uint32_t              structSize;
        uint32_t              flags;
        std::atomic<uint32_t> cleanShutdown;
        DWORD                 crashingPid;
        DWORD                 crashingTid;
        uint64_t              exceptionPointersAddr;
        EXCEPTION_RECORD      exceptionRecord;
        CONTEXT               contextRecord;
        wchar_t               appName[64];
        wchar_t               dumpDir[MAX_PATH];
        wchar_t               exePath[MAX_PATH];
        char                  buildInfo[128];
        char                  reason[512];
    };

    static_assert(std::atomic<uint32_t>::is_always_lock_free,
        "Shared-memory shutdown flag must be lock-free across processes");
}

#endif
