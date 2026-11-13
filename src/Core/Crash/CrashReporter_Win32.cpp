#include "pch.h"
#include <Engine/CrashReporter.h>
#include "CrashProtocol.h"

#ifdef PLATFORM_WINDOWS

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <eh.h>
#include <exception>
#include <stdlib.h>

#ifndef ZERO_BUILD_CONFIG_STR
    #if defined(ZERO_DEBUG) || !defined(NDEBUG)
        #define ZERO_BUILD_CONFIG_STR "Debug"
    #else
        #define ZERO_BUILD_CONFIG_STR "RelWithDebInfo/Release"
    #endif
#endif

namespace
{
    using namespace Zero::CrashInternal;

    struct CrashReporterState
    {
        HANDLE                         hMapFile       = nullptr;
        CrashSharedBlock*              shared         = nullptr;
        HANDLE                         hCrashEvent    = nullptr;
        HANDLE                         hAckEvent      = nullptr;
        HANDLE                         hTargetProcess = nullptr;
        HANDLE                         hHelperProcess = nullptr;
        std::atomic_flag               crashing       = ATOMIC_FLAG_INIT;
        bool                           initialized    = false;
        LPTOP_LEVEL_EXCEPTION_FILTER   prevFilter     = nullptr;
        std::terminate_handler         prevTerminate  = nullptr;
        _purecall_handler              prevPureCall   = nullptr;
        _invalid_parameter_handler     prevInvalidPar = nullptr;
        void (*prevAbort)(int)                        = nullptr;
    };

    CrashReporterState g_state;

    bool FileExistsW(const wchar_t* path)
    {
        DWORD attrs = GetFileAttributesW(path);
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    template<typename CharT>
    void CopyBounded(CharT* dst, size_t dstCap, const CharT* src)
    {
        if (!dst || dstCap == 0)
            return;
        size_t i = 0;
        if (src)
        {
            for (; i + 1 < dstCap && src[i] != CharT(0); ++i)
                dst[i] = src[i];
        }
        dst[i] = CharT(0);
    }

    void Utf8ToWideBounded(wchar_t* dst, size_t dstCap, const char* src)
    {
        if (!dst || dstCap == 0)
            return;
        if (!src || src[0] == '\0')
        {
            dst[0] = L'\0';
            return;
        }
        int written = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, static_cast<int>(dstCap));
        if (written <= 0)
            dst[0] = L'\0';
        dst[dstCap - 1] = L'\0';
    }

    void ExtractParentDir(wchar_t* dst, size_t dstCap, const wchar_t* filePath)
    {
        CopyBounded(dst, dstCap, filePath);
        wchar_t* lastSlash = nullptr;
        for (wchar_t* p = dst; *p != L'\0'; ++p)
        {
            if (*p == L'\\' || *p == L'/')
                lastSlash = p;
        }
        if (lastSlash)
            *lastSlash = L'\0';
        else if (dstCap > 0)
            dst[0] = L'\0';
    }

    void JoinPathW(wchar_t* dst, size_t dstCap, const wchar_t* dir, const wchar_t* leaf)
    {
        size_t i = 0;
        if (dir)
        {
            for (; i + 1 < dstCap && dir[i] != L'\0'; ++i)
                dst[i] = dir[i];
        }
        if (i > 0 && i + 1 < dstCap && dst[i - 1] != L'\\' && dst[i - 1] != L'/')
            dst[i++] = L'\\';
        if (leaf)
        {
            for (size_t j = 0; i + 1 < dstCap && leaf[j] != L'\0'; ++j, ++i)
                dst[i] = leaf[j];
        }
        dst[i] = L'\0';
    }

    bool ResolveHelperExe(wchar_t* outPath, size_t outCap, const wchar_t* exeDir, const char* overridePathUtf8)
    {
        if (overridePathUtf8 && overridePathUtf8[0] != '\0')
        {
            wchar_t wideOverride[MAX_PATH]{};
            Utf8ToWideBounded(wideOverride, MAX_PATH, overridePathUtf8);
            if (wideOverride[0] != L'\0')
            {
                DWORD len = GetFullPathNameW(wideOverride, static_cast<DWORD>(outCap), outPath, nullptr);
                if (len > 0 && len < outCap && FileExistsW(outPath))
                    return true;
            }
        }

        const wchar_t* candidates[] = {
            L"ZeroCrashHandler.exe",
            L"..\\ZeroCrashHandler.exe",
            L"..\\..\\ZeroCrashHandler.exe"
        };

        wchar_t joined[MAX_PATH]{};
        for (const wchar_t* rel : candidates)
        {
            JoinPathW(joined, MAX_PATH, exeDir, rel);
            DWORD len = GetFullPathNameW(joined, static_cast<DWORD>(outCap), outPath, nullptr);
            if (len > 0 && len < outCap && FileExistsW(outPath))
                return true;
        }
        return false;
    }

    void CloseSharedHandles()
    {
        if (g_state.shared)
        {
            UnmapViewOfFile(g_state.shared);
            g_state.shared = nullptr;
        }
        auto closeIf = [](HANDLE& h)
        {
            if (h)
            {
                CloseHandle(h);
                h = nullptr;
            }
        };
        closeIf(g_state.hHelperProcess);
        closeIf(g_state.hTargetProcess);
        closeIf(g_state.hCrashEvent);
        closeIf(g_state.hAckEvent);
        closeIf(g_state.hMapFile);
    }

    void AppendLiteral(wchar_t* dst, size_t cap, size_t& pos, const wchar_t* lit)
    {
        for (size_t i = 0; lit[i] != L'\0' && pos + 1 < cap; ++i)
            dst[pos++] = lit[i];
        dst[pos] = L'\0';
    }

    void AppendHandleValue(wchar_t* dst, size_t cap, size_t& pos, HANDLE h)
    {
        uintptr_t val = reinterpret_cast<uintptr_t>(h);
        wchar_t tmp[32]{};
        size_t len = 0;
        do
        {
            tmp[len++] = static_cast<wchar_t>(L'0' + (val % 10));
            val /= 10;
        } while (val != 0 && len < 32);

        while (len > 0 && pos + 1 < cap)
            dst[pos++] = tmp[--len];
        dst[pos] = L'\0';
    }

    bool LaunchHelper(const wchar_t* helperExe)
    {
        HANDLE hChildStderr = nullptr;
        HANDLE hCurrentStderr = GetStdHandle(STD_ERROR_HANDLE);
        if (hCurrentStderr && hCurrentStderr != INVALID_HANDLE_VALUE)
        {
            HANDLE tmp = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), hCurrentStderr, GetCurrentProcess(),
                    &tmp, 0, TRUE, DUPLICATE_SAME_ACCESS))
            {
                hChildStderr = tmp;
            }
        }

        HANDLE inheritHandles[5]{
            g_state.hTargetProcess,
            g_state.hMapFile,
            g_state.hCrashEvent,
            g_state.hAckEvent,
            hChildStderr
        };
        DWORD handleCount = hChildStderr ? 5u : 4u;

        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        if (attrSize == 0 || attrSize > 512)
        {
            if (hChildStderr) CloseHandle(hChildStderr);
            return false;
        }

        alignas(16) unsigned char attrBuffer[512]{};
        auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer);
        if (!InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize))
        {
            if (hChildStderr) CloseHandle(hChildStderr);
            return false;
        }

        if (!UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inheritHandles, sizeof(HANDLE) * handleCount, nullptr, nullptr))
        {
            DeleteProcThreadAttributeList(attrList);
            if (hChildStderr) CloseHandle(hChildStderr);
            return false;
        }

        wchar_t cmdLine[MAX_PATH * 2]{};
        size_t pos = 0;
        AppendLiteral(cmdLine, MAX_PATH * 2, pos, L"\"");
        AppendLiteral(cmdLine, MAX_PATH * 2, pos, helperExe);
        AppendLiteral(cmdLine, MAX_PATH * 2, pos, L"\" --proc ");
        AppendHandleValue(cmdLine, MAX_PATH * 2, pos, g_state.hTargetProcess);
        AppendLiteral(cmdLine, MAX_PATH * 2, pos, L" --shm ");
        AppendHandleValue(cmdLine, MAX_PATH * 2, pos, g_state.hMapFile);
        AppendLiteral(cmdLine, MAX_PATH * 2, pos, L" --crash-evt ");
        AppendHandleValue(cmdLine, MAX_PATH * 2, pos, g_state.hCrashEvent);
        AppendLiteral(cmdLine, MAX_PATH * 2, pos, L" --ack-evt ");
        AppendHandleValue(cmdLine, MAX_PATH * 2, pos, g_state.hAckEvent);
        if (hChildStderr)
        {
            AppendLiteral(cmdLine, MAX_PATH * 2, pos, L" --stderr ");
            AppendHandleValue(cmdLine, MAX_PATH * 2, pos, hChildStderr);
        }

        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        si.lpAttributeList = attrList;

        DWORD creationFlags = EXTENDED_STARTUPINFO_PRESENT;
        if (GetConsoleWindow() == nullptr && hChildStderr == nullptr)
            creationFlags |= CREATE_NO_WINDOW;

        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(
            helperExe,
            cmdLine,
            nullptr,
            nullptr,
            TRUE,
            creationFlags,
            nullptr,
            nullptr,
            &si.StartupInfo,
            &pi);

        DeleteProcThreadAttributeList(attrList);
        if (hChildStderr)
            CloseHandle(hChildStderr);

        if (!ok)
            return false;

        CloseHandle(pi.hThread);
        g_state.hHelperProcess = pi.hProcess;
        return true;
    }

    // Runs in the crashing thread: avoid heap allocations, C++ formatting, or spdlog.
    void TriggerOutOfProcessDump(EXCEPTION_POINTERS* ep, const char* reason)
    {
        if (g_state.crashing.test_and_set(std::memory_order_acq_rel))
        {
            // Keep secondary crashing threads frozen so they do not exit before the dump completes.
            Sleep(INFINITE);
            return;
        }

        if (!g_state.initialized || !g_state.shared || !g_state.hCrashEvent || !g_state.hHelperProcess)
            return;

        if (WaitForSingleObject(g_state.hHelperProcess, 0) != WAIT_TIMEOUT)
            return;

        EXCEPTION_RECORD localRec{};
        CONTEXT localCtx{};
        EXCEPTION_POINTERS localEp{};

        if (!ep)
        {
            RtlCaptureContext(&localCtx);
            localRec.ExceptionCode    = kFatalReportExceptionCode;
            localRec.ExceptionFlags   = EXCEPTION_NONCONTINUABLE;
            localRec.ExceptionAddress = reinterpret_cast<PVOID>(localCtx.Rip);
            localEp.ExceptionRecord   = &localRec;
            localEp.ContextRecord     = &localCtx;
            ep = &localEp;
        }

        CrashSharedBlock* shared = g_state.shared;
        shared->crashingPid = GetCurrentProcessId();
        shared->crashingTid = GetCurrentThreadId();
        CopyBounded(shared->reason, sizeof(shared->reason), reason);

        shared->exceptionPointersAddr = reinterpret_cast<uint64_t>(ep);
        shared->flags |= CrashFlag_HasExceptionPointers;

        if (ep->ExceptionRecord)
        {
            std::memcpy(&shared->exceptionRecord, ep->ExceptionRecord, sizeof(EXCEPTION_RECORD));
            shared->flags |= CrashFlag_HasExceptionRecord;
        }
        if (ep->ContextRecord)
        {
            std::memcpy(&shared->contextRecord, ep->ContextRecord, sizeof(CONTEXT));
            shared->flags |= CrashFlag_HasContextRecord;
        }

        std::atomic_thread_fence(std::memory_order_release);
        SetEvent(g_state.hCrashEvent);

        if (g_state.hAckEvent)
            WaitForSingleObject(g_state.hAckEvent, 15000);
    }

    LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* ep)
    {
        TriggerOutOfProcessDump(ep, nullptr);
        if (g_state.prevFilter && g_state.prevFilter != OnUnhandledException)
            return g_state.prevFilter(ep);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    [[noreturn]] void TerminateHard(const char* reason)
    {
        TriggerOutOfProcessDump(nullptr, reason);
        TerminateProcess(GetCurrentProcess(), kFatalReportExceptionCode);
        for (;;)
            Sleep(INFINITE);
    }

    void OnTerminate()
    {
        TerminateHard("std::terminate() called");
    }

    void OnPureCall()
    {
        TerminateHard("Pure virtual function call (_purecall)");
    }

    void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        TerminateHard("CRT invalid parameter handler triggered");
    }

    void OnAbortSignal(int)
    {
        TerminateHard("SIGABRT received");
    }
}

namespace Zero
{
    void CrashReporter::Init(const CrashReporterConfig& config)
    {
        if (g_state.initialized)
        {
            if (config.appName)
                SetAppName(config.appName);
            return;
        }

        ULONG stackGuarantee = 32u * 1024u;
        SetThreadStackGuarantee(&stackGuarantee);

        wchar_t exePath[MAX_PATH]{};
        DWORD exeLen = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (exeLen == 0 || exeLen >= MAX_PATH)
            return;

        wchar_t exeDir[MAX_PATH]{};
        ExtractParentDir(exeDir, MAX_PATH, exePath);

        wchar_t helperPath[MAX_PATH]{};
        if (!ResolveHelperExe(helperPath, MAX_PATH, exeDir, config.helperExe))
            return;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength        = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;

        g_state.hCrashEvent = CreateEventW(&sa, TRUE, FALSE, nullptr);
        g_state.hAckEvent   = CreateEventW(&sa, TRUE, FALSE, nullptr);
        g_state.hMapFile    = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
                                0, sizeof(CrashInternal::CrashSharedBlock), nullptr);

        if (!g_state.hCrashEvent || !g_state.hAckEvent || !g_state.hMapFile)
        {
            CloseSharedHandles();
            return;
        }

        g_state.shared = static_cast<CrashInternal::CrashSharedBlock*>(
            MapViewOfFile(g_state.hMapFile, FILE_MAP_WRITE, 0, 0, sizeof(CrashInternal::CrashSharedBlock)));
        if (!g_state.shared)
        {
            CloseSharedHandles();
            return;
        }

        if (!DuplicateHandle(
                GetCurrentProcess(),
                GetCurrentProcess(),
                GetCurrentProcess(),
                &g_state.hTargetProcess,
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE | SYNCHRONIZE,
                TRUE,
                0))
        {
            CloseSharedHandles();
            return;
        }

        std::memset(g_state.shared, 0, sizeof(CrashInternal::CrashSharedBlock));
        g_state.shared->magic      = CrashInternal::kCrashMagic;
        g_state.shared->version    = CrashInternal::kCrashVersion;
        g_state.shared->structSize = static_cast<uint32_t>(sizeof(CrashInternal::CrashSharedBlock));
        g_state.shared->flags      = config.fullMemoryDump
            ? CrashInternal::CrashFlag_FullMemoryDump
            : CrashInternal::CrashFlag_None;
        g_state.shared->cleanShutdown.store(0u, std::memory_order_relaxed);

        CopyBounded(g_state.shared->exePath, MAX_PATH, exePath);
        Utf8ToWideBounded(g_state.shared->appName, 64, config.appName ? config.appName : "Zero");

        wchar_t wideDumpDir[MAX_PATH]{};
        if (config.dumpDir && config.dumpDir[0] != '\0')
            Utf8ToWideBounded(wideDumpDir, MAX_PATH, config.dumpDir);

        if (wideDumpDir[0] != L'\0')
        {
            DWORD len = GetFullPathNameW(wideDumpDir, MAX_PATH, g_state.shared->dumpDir, nullptr);
            if (len == 0 || len >= MAX_PATH)
                CopyBounded(g_state.shared->dumpDir, MAX_PATH, wideDumpDir);
        }
        else
        {
            JoinPathW(g_state.shared->dumpDir, MAX_PATH, exeDir, L"crashes");
        }

#if defined(__clang__)
        CopyBounded(g_state.shared->buildInfo, sizeof(g_state.shared->buildInfo),
            ZERO_BUILD_CONFIG_STR " (Clang)");
#elif defined(_MSC_VER)
        CopyBounded(g_state.shared->buildInfo, sizeof(g_state.shared->buildInfo),
            ZERO_BUILD_CONFIG_STR " (MSVC)");
#else
        CopyBounded(g_state.shared->buildInfo, sizeof(g_state.shared->buildInfo),
            ZERO_BUILD_CONFIG_STR);
#endif

        if (!LaunchHelper(helperPath))
        {
            CloseSharedHandles();
            return;
        }

        g_state.prevFilter     = SetUnhandledExceptionFilter(OnUnhandledException);
        g_state.prevTerminate  = std::set_terminate(OnTerminate);
        g_state.prevPureCall   = _set_purecall_handler(OnPureCall);
        g_state.prevInvalidPar = _set_invalid_parameter_handler(OnInvalidParameter);
        g_state.prevAbort      = std::signal(SIGABRT, OnAbortSignal);
        g_state.initialized    = true;
    }

    void CrashReporter::Shutdown()
    {
        if (!g_state.initialized)
            return;

        SetUnhandledExceptionFilter(g_state.prevFilter);
        std::set_terminate(g_state.prevTerminate);
        _set_purecall_handler(g_state.prevPureCall);
        _set_invalid_parameter_handler(g_state.prevInvalidPar);
        if (g_state.prevAbort != SIG_ERR)
            std::signal(SIGABRT, g_state.prevAbort);

        if (g_state.shared && g_state.hCrashEvent)
        {
            g_state.shared->cleanShutdown.store(1u, std::memory_order_release);
            SetEvent(g_state.hCrashEvent);
        }

        if (g_state.hHelperProcess)
            WaitForSingleObject(g_state.hHelperProcess, 2000);

        CloseSharedHandles();
        g_state.crashing.clear(std::memory_order_relaxed);
        g_state.initialized = false;
    }

    void CrashReporter::SetAppName(const char* appName)
    {
        if (g_state.initialized && g_state.shared && appName)
            Utf8ToWideBounded(g_state.shared->appName, 64, appName);
    }

    void CrashReporter::ReportFatal(const char* reason)
    {
        TerminateHard(reason ? reason : "Fatal engine error");
    }
}

#endif
