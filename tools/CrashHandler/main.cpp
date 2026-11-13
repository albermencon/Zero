#include "CrashProtocol.h"

#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

namespace
{
    using namespace Zero::CrashInternal;

    using FnGetThreadDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);

    struct ParsedArgs
    {
        HANDLE hTargetProcess = nullptr;
        HANDLE hMapFile       = nullptr;
        HANDLE hCrashEvent    = nullptr;
        HANDLE hAckEvent      = nullptr;
        HANDLE hStderr        = nullptr;
    };

    bool ParseHandleArg(const char* text, HANDLE& out)
    {
        if (!text || text[0] == '\0')
            return false;
        char* end = nullptr;
        unsigned long long val = std::strtoull(text, &end, 10);
        if (end == text || *end != '\0' || val == 0)
            return false;
        out = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(val));
        return true;
    }

    bool ParseArgs(int argc, char** argv, ParsedArgs& out)
    {
        for (int i = 1; i + 1 < argc; i += 2)
        {
            std::string_view flag(argv[i]);
            const char* val = argv[i + 1];

            if (flag == "--proc")
            {
                if (!ParseHandleArg(val, out.hTargetProcess)) return false;
            }
            else if (flag == "--shm")
            {
                if (!ParseHandleArg(val, out.hMapFile)) return false;
            }
            else if (flag == "--crash-evt")
            {
                if (!ParseHandleArg(val, out.hCrashEvent)) return false;
            }
            else if (flag == "--ack-evt")
            {
                if (!ParseHandleArg(val, out.hAckEvent)) return false;
            }
            else if (flag == "--stderr")
            {
                if (!ParseHandleArg(val, out.hStderr)) return false;
            }
            else
            {
                return false;
            }
        }
        return out.hTargetProcess && out.hMapFile && out.hCrashEvent && out.hAckEvent;
    }

    std::string WideToUtf8(std::wstring_view ws)
    {
        if (ws.empty())
            return {};
        int needed = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            return {};
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), out.data(), needed, nullptr, nullptr);
        return out;
    }

    void EnsureDirectoryRecursive(std::wstring_view dir)
    {
        if (dir.empty())
            return;
        std::wstring accum;
        accum.reserve(dir.size() + 1);
        for (size_t i = 0; i < dir.size(); ++i)
        {
            wchar_t c = dir[i];
            accum.push_back(c);
            if ((c == L'\\' || c == L'/') && accum.size() > 3)
            {
                CreateDirectoryW(accum.c_str(), nullptr);
            }
        }
        CreateDirectoryW(accum.c_str(), nullptr);
    }

    std::wstring ExtractDirW(const wchar_t* path)
    {
        std::wstring s(path ? path : L"");
        size_t pos = s.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
            return {};
        return s.substr(0, pos);
    }

    void WriteAll(HANDLE hFile, std::string_view text)
    {
        if (!hFile || hFile == INVALID_HANDLE_VALUE || text.empty())
            return;
        const char* ptr = text.data();
        size_t remaining = text.size();
        while (remaining > 0)
        {
            DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(64 * 1024)));
            DWORD written = 0;
            if (!WriteFile(hFile, ptr, chunk, &written, nullptr) || written == 0)
                break;
            ptr += written;
            remaining -= written;
        }
    }

    void AppendFormatted(std::string& dst, const char* fmt, ...)
    {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        int len = std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (len <= 0)
            return;
        size_t count = static_cast<size_t>((std::min)(len, static_cast<int>(sizeof(buf) - 1)));
        dst.append(buf, count);
    }

    const char* ExceptionCodeName(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
        case kFatalReportExceptionCode:          return "ZERO_FATAL_ABORT";
        default:                                 return "UNKNOWN_EXCEPTION";
        }
    }

    const char* AccessOpName(ULONG_PTR op)
    {
        switch (op)
        {
        case 0:  return "READ";
        case 1:  return "WRITE";
        case 8:  return "DEP_EXECUTE";
        default: return "ACCESS";
        }
    }

    std::string GetThreadNameUtf8(HANDLE hThread, FnGetThreadDescription getDescFn)
    {
        if (!getDescFn || !hThread)
            return {};
        PWSTR wideDesc = nullptr;
        if (SUCCEEDED(getDescFn(hThread, &wideDesc)) && wideDesc)
        {
            std::string name = WideToUtf8(wideDesc);
            LocalFree(wideDesc);
            return name;
        }
        return {};
    }

    void AppendModuleRva(HANDLE hProc, DWORD64 addr, std::string& out)
    {
        IMAGEHLP_MODULE64 mod{};
        mod.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        if (SymGetModuleInfo64(hProc, addr, &mod))
        {
            DWORD64 rva = addr - mod.BaseOfImage;
            AppendFormatted(out, "%s+0x%llX", mod.ModuleName, static_cast<unsigned long long>(rva));
        }
        else
        {
            AppendFormatted(out, "0x%016llX", static_cast<unsigned long long>(addr));
        }
    }

    void AppendFrameLine(HANDLE hProc, DWORD64 lookupAddr, DWORD inlineCtx,
        int frameIdx, bool isInline, std::string& out)
    {
        alignas(SYMBOL_INFO) char symStorage[sizeof(SYMBOL_INFO) + 512]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symStorage);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 511;

        DWORD64 disp = 0;
        BOOL hasSym = FALSE;
        if (inlineCtx != INLINE_FRAME_CONTEXT_IGNORE)
            hasSym = SymFromInlineContext(hProc, lookupAddr, inlineCtx, &disp, sym);
        if (!hasSym)
            hasSym = SymFromAddr(hProc, lookupAddr, &disp, sym);

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisp = 0;
        BOOL hasLine = FALSE;
        if (inlineCtx != INLINE_FRAME_CONTEXT_IGNORE)
            hasLine = SymGetLineFromInlineContext(hProc, lookupAddr, inlineCtx, 0, &lineDisp, &line);
        if (!hasLine)
            hasLine = SymGetLineFromAddr64(hProc, lookupAddr, &lineDisp, &line);

        IMAGEHLP_MODULE64 mod{};
        mod.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        BOOL hasMod = SymGetModuleInfo64(hProc, lookupAddr, &mod);

        AppendFormatted(out, "  [%02d] ", frameIdx);
        if (hasMod)
            AppendFormatted(out, "%s!", mod.ModuleName);

        if (hasSym)
            AppendFormatted(out, "%s+0x%llX", sym->Name, static_cast<unsigned long long>(disp));
        else
            AppendFormatted(out, "0x%016llX", static_cast<unsigned long long>(lookupAddr));

        if (hasLine)
            AppendFormatted(out, " (%s:%lu)", line.FileName, line.LineNumber);

        if (isInline)
            out.append(" [inline]");

        out.push_back('\n');
    }

    void WalkThreadStack(HANDLE hProc, HANDLE hThread, const CONTEXT& initialCtx, std::string& out)
    {
        CONTEXT ctx = initialCtx;
        STACKFRAME_EX frame{};
        frame.StackFrameSize     = sizeof(STACKFRAME_EX);
        frame.InlineFrameContext = INLINE_FRAME_CONTEXT_INIT;
        frame.AddrPC.Offset      = ctx.Rip;
        frame.AddrPC.Mode        = AddrModeFlat;
        frame.AddrFrame.Offset   = ctx.Rbp;
        frame.AddrFrame.Mode     = AddrModeFlat;
        frame.AddrStack.Offset   = ctx.Rsp;
        frame.AddrStack.Mode     = AddrModeFlat;

        const DWORD64 entryRip = ctx.Rip;
        DWORD64 prevPc    = 0;
        DWORD64 prevStack = 0;

        constexpr int kMaxFrames = 128;
        for (int i = 0; i < kMaxFrames; ++i)
        {
            if (!StackWalkEx(
                    IMAGE_FILE_MACHINE_AMD64,
                    hProc,
                    hThread,
                    &frame,
                    &ctx,
                    nullptr,
                    SymFunctionTableAccess64,
                    SymGetModuleBase64,
                    nullptr,
                    SYM_STKWALK_DEFAULT))
            {
                break;
            }

            if (frame.AddrPC.Offset == 0)
                break;

            const bool isInline = (i > 0 && frame.AddrPC.Offset == prevPc && frame.AddrStack.Offset == prevStack);
            const DWORD64 lookupAddr = (frame.AddrPC.Offset != entryRip && frame.AddrPC.Offset > 0)
                ? (frame.AddrPC.Offset - 1)
                : frame.AddrPC.Offset;

            AppendFrameLine(hProc, lookupAddr, frame.InlineFrameContext, i, isInline, out);

            prevPc    = frame.AddrPC.Offset;
            prevStack = frame.AddrStack.Offset;
        }
    }

    bool WriteMinidumpFile(HANDLE hTargetProc, const CrashSharedBlock* shared,
        const std::wstring& dumpPath, DWORD& outError)
    {
        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            outError = GetLastError();
            return false;
        }

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        const bool hasMei = (shared->flags & CrashFlag_HasExceptionPointers) &&
                            shared->exceptionPointersAddr != 0;
        if (hasMei)
        {
            mei.ThreadId          = shared->crashingTid;
            mei.ExceptionPointers = reinterpret_cast<PEXCEPTION_POINTERS>(shared->exceptionPointersAddr);
            mei.ClientPointers    = TRUE;
        }

        MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal |
            MiniDumpWithThreadInfo |
            MiniDumpWithIndirectlyReferencedMemory |
            ((shared->flags & CrashFlag_FullMemoryDump) ? MiniDumpWithFullMemory : 0));

        BOOL ok = MiniDumpWriteDump(
            hTargetProc,
            shared->crashingPid,
            hFile,
            dumpType,
            hasMei ? &mei : nullptr,
            nullptr,
            nullptr);

        outError = ok ? 0 : GetLastError();
        CloseHandle(hFile);
        return ok != FALSE;
    }

    void AppendHeaderAndException(HANDLE hTargetProc, const CrashSharedBlock* shared,
        const SYSTEMTIME& st, const std::string& dumpPathUtf8, bool dumpOk, DWORD dumpErr,
        std::string& out)
    {
        std::string appUtf8 = WideToUtf8(shared->appName);
        std::string exeUtf8 = WideToUtf8(shared->exePath);

        out.append("\n==================== CRASH DETECTED ====================\n");
        AppendFormatted(out, "Timestamp:   %04u-%02u-%02u %02u:%02u:%02u.%03u\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        AppendFormatted(out, "Application: %s (%s)\n",
            appUtf8.empty() ? "Zero" : appUtf8.c_str(),
            shared->buildInfo[0] ? shared->buildInfo : "unknown");
        AppendFormatted(out, "Executable:  %s\n", exeUtf8.c_str());
        AppendFormatted(out, "Process/TID: PID %lu / TID %lu\n",
            static_cast<unsigned long>(shared->crashingPid),
            static_cast<unsigned long>(shared->crashingTid));

        if (shared->reason[0] != '\0')
            AppendFormatted(out, "Reason:      %s\n", shared->reason);

        if (shared->flags & CrashFlag_HasExceptionRecord)
        {
            const EXCEPTION_RECORD& rec = shared->exceptionRecord;
            DWORD64 faultPc = reinterpret_cast<DWORD64>(rec.ExceptionAddress);
            AppendFormatted(out, "Exception:   0x%08lX (%s)\n",
                static_cast<unsigned long>(rec.ExceptionCode),
                ExceptionCodeName(rec.ExceptionCode));
            out.append("Fault Site:  ");
            AppendModuleRva(hTargetProc, faultPc, out);
            AppendFormatted(out, " (0x%016llX)\n", static_cast<unsigned long long>(faultPc));

            if ((rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
                 rec.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
                rec.NumberParameters >= 2)
            {
                AppendFormatted(out, "Violation:   %s at address 0x%016llX\n",
                    AccessOpName(rec.ExceptionInformation[0]),
                    static_cast<unsigned long long>(rec.ExceptionInformation[1]));
            }
        }

        if (dumpOk)
            AppendFormatted(out, "Minidump:    %s\n", dumpPathUtf8.c_str());
        else
            AppendFormatted(out, "Minidump:    FAILED (Win32 error %lu) -> %s\n",
                static_cast<unsigned long>(dumpErr), dumpPathUtf8.c_str());
    }

    void AppendRegisters(const CONTEXT& c, std::string& out)
    {
        out.append("\n[Registers]\n");
        AppendFormatted(out, "  RIP=0x%016llX  RSP=0x%016llX  RBP=0x%016llX  EFL=0x%08lX\n",
            static_cast<unsigned long long>(c.Rip),
            static_cast<unsigned long long>(c.Rsp),
            static_cast<unsigned long long>(c.Rbp),
            static_cast<unsigned long>(c.EFlags));
        AppendFormatted(out, "  RAX=0x%016llX  RBX=0x%016llX  RCX=0x%016llX  RDX=0x%016llX\n",
            static_cast<unsigned long long>(c.Rax),
            static_cast<unsigned long long>(c.Rbx),
            static_cast<unsigned long long>(c.Rcx),
            static_cast<unsigned long long>(c.Rdx));
        AppendFormatted(out, "  RSI=0x%016llX  RDI=0x%016llX  R8 =0x%016llX  R9 =0x%016llX\n",
            static_cast<unsigned long long>(c.Rsi),
            static_cast<unsigned long long>(c.Rdi),
            static_cast<unsigned long long>(c.R8),
            static_cast<unsigned long long>(c.R9));
        AppendFormatted(out, "  R10=0x%016llX  R11=0x%016llX  R12=0x%016llX  R13=0x%016llX\n",
            static_cast<unsigned long long>(c.R10),
            static_cast<unsigned long long>(c.R11),
            static_cast<unsigned long long>(c.R12),
            static_cast<unsigned long long>(c.R13));
        AppendFormatted(out, "  R14=0x%016llX  R15=0x%016llX\n",
            static_cast<unsigned long long>(c.R14),
            static_cast<unsigned long long>(c.R15));
    }

    void AppendOtherThreads(HANDLE hTargetProc, DWORD pid, DWORD crashingTid,
        FnGetThreadDescription getDescFn, std::string& fullLogOut)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return;

        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (!Thread32First(snap, &te))
        {
            CloseHandle(snap);
            return;
        }

        fullLogOut.append("\n[Other Live Threads]\n");
        do
        {
            if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(te.th32OwnerProcessID) &&
                te.th32OwnerProcessID == pid &&
                te.th32ThreadID != crashingTid)
            {
                HANDLE hThread = OpenThread(
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION,
                    FALSE, te.th32ThreadID);
                if (!hThread)
                    continue;

                std::string tname = GetThreadNameUtf8(hThread, getDescFn);
                AppendFormatted(fullLogOut, "\nThread %lu", static_cast<unsigned long>(te.th32ThreadID));
                if (!tname.empty())
                    AppendFormatted(fullLogOut, " (\"%s\")", tname.c_str());
                fullLogOut.push_back('\n');

                DWORD suspendCount = SuspendThread(hThread);
                if (suspendCount != static_cast<DWORD>(-1))
                {
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(hThread, &ctx))
                        WalkThreadStack(hTargetProc, hThread, ctx, fullLogOut);
                    ResumeThread(hThread);
                }
                CloseHandle(hThread);
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap, &te));

        CloseHandle(snap);
    }

    int HandleCrash(const ParsedArgs& args, const CrashSharedBlock* shared)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);

        EnsureDirectoryRecursive(shared->dumpDir);

        wchar_t baseName[512]{};
        const wchar_t* appName = shared->appName[0] ? shared->appName : L"Zero";
        std::swprintf(baseName, 512, L"%ls\\%ls_%04u%02u%02u_%02u%02u%02u_%03u_pid%lu",
            shared->dumpDir, appName,
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            static_cast<unsigned long>(shared->crashingPid));

        std::wstring dumpPath = std::wstring(baseName) + L".dmp";
        std::wstring logPath  = std::wstring(baseName) + L".log";

        // Write the minidump first before symbol resolution or suspending threads externally.
        DWORD dumpErr = 0;
        const bool dumpOk = WriteMinidumpFile(args.hTargetProcess, shared, dumpPath, dumpErr);

        std::wstring symPath = ExtractDirW(shared->exePath);
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                      SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS);
        SymInitializeW(args.hTargetProcess, symPath.empty() ? nullptr : symPath.c_str(), TRUE);

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto getDescFn = kernel32
            ? reinterpret_cast<FnGetThreadDescription>(GetProcAddress(kernel32, "GetThreadDescription"))
            : nullptr;

        std::string dumpPathUtf8 = WideToUtf8(dumpPath);
        std::string logPathUtf8  = WideToUtf8(logPath);

        std::string consoleSummary;
        consoleSummary.reserve(4096);
        AppendHeaderAndException(args.hTargetProcess, shared, st, dumpPathUtf8, dumpOk, dumpErr, consoleSummary);
        AppendFormatted(consoleSummary, "Crash Log:   %s\n", logPathUtf8.c_str());

        if (shared->flags & CrashFlag_HasContextRecord)
            AppendRegisters(shared->contextRecord, consoleSummary);

        HANDLE hCrashThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_GET_CONTEXT,
            FALSE, shared->crashingTid);
        std::string crashThreadName = GetThreadNameUtf8(hCrashThread, getDescFn);

        AppendFormatted(consoleSummary, "\n[Crashing Thread %lu%s%s%s]\n",
            static_cast<unsigned long>(shared->crashingTid),
            crashThreadName.empty() ? "" : " (\"",
            crashThreadName.c_str(),
            crashThreadName.empty() ? "" : "\")");

        if (shared->flags & CrashFlag_HasContextRecord)
            WalkThreadStack(args.hTargetProcess, hCrashThread, shared->contextRecord, consoleSummary);

        if (hCrashThread)
            CloseHandle(hCrashThread);

        std::string fullReport = consoleSummary;
        AppendOtherThreads(args.hTargetProcess, shared->crashingPid, shared->crashingTid, getDescFn, fullReport);

        HANDLE hLogFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hLogFile != INVALID_HANDLE_VALUE)
        {
            WriteAll(hLogFile, fullReport);
            FlushFileBuffers(hLogFile);
            CloseHandle(hLogFile);
        }

        consoleSummary.append("========================================================\n");
        HANDLE hStderr = args.hStderr ? args.hStderr : GetStdHandle(STD_ERROR_HANDLE);
        WriteAll(hStderr, consoleSummary);

        SymCleanup(args.hTargetProcess);
        SetEvent(args.hAckEvent);
        return 0;
    }
}

int main(int argc, char** argv)
{
    ParsedArgs args{};
    if (!ParseArgs(argc, argv, args))
        return 1;

    auto* shared = static_cast<const Zero::CrashInternal::CrashSharedBlock*>(
        MapViewOfFile(args.hMapFile, FILE_MAP_READ, 0, 0, sizeof(Zero::CrashInternal::CrashSharedBlock)));

    if (!shared ||
        shared->magic != Zero::CrashInternal::kCrashMagic ||
        shared->version != Zero::CrashInternal::kCrashVersion ||
        shared->structSize != sizeof(Zero::CrashInternal::CrashSharedBlock))
    {
        if (shared) UnmapViewOfFile(shared);
        return 2;
    }

    HANDLE waits[2] = { args.hCrashEvent, args.hTargetProcess };
    DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

    int exitCode = 0;
    if (waitResult == WAIT_OBJECT_0 &&
        shared->cleanShutdown.load(std::memory_order_acquire) == 0)
    {
        std::atomic_thread_fence(std::memory_order_acquire);
        exitCode = HandleCrash(args, shared);
    }

    UnmapViewOfFile(shared);
    CloseHandle(args.hTargetProcess);
    CloseHandle(args.hMapFile);
    CloseHandle(args.hCrashEvent);
    CloseHandle(args.hAckEvent);
    if (args.hStderr)
        CloseHandle(args.hStderr);

    return exitCode;
}
