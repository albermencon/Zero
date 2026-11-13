#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <Engine/Core.h>
#include <Engine/CrashReporter.h>
#include <Engine/Log.h>
#include <Engine/Thread/Thread.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

namespace
{
    std::string PathToUtf8(const std::filesystem::path& p)
    {
        std::u8string u8 = p.u8string();
        return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
    }

    std::filesystem::path MakeUniqueTempDir(const wchar_t* tag)
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::wstring name = std::wstring(L"ZeroCrashTest_") + tag + L"_" +
                            std::to_wstring(GetCurrentProcessId()) + L"_" +
                            std::to_wstring(stamp);
        std::filesystem::path p = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(p);
        return p;
    }

    struct TempDirGuard
    {
        std::filesystem::path path;
        ~TempDirGuard()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };

    struct CrashArtifacts
    {
        std::filesystem::path dmpPath;
        std::filesystem::path logPath;
        std::string           logText;
    };

    CrashArtifacts CollectArtifacts(const std::filesystem::path& dir)
    {
        CrashArtifacts result;
        if (!std::filesystem::exists(dir))
            return result;

        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.path().extension() == L".dmp")
                result.dmpPath = entry.path();
            else if (entry.path().extension() == L".log")
                result.logPath = entry.path();
        }

        if (!result.logPath.empty())
        {
            std::ifstream ifs(result.logPath, std::ios::binary);
            std::ostringstream oss;
            oss << ifs.rdbuf();
            result.logText = oss.str();
        }
        return result;
    }

    bool VerifyMinidumpHeader(const std::filesystem::path& dmpPath)
    {
        if (dmpPath.empty() || !std::filesystem::exists(dmpPath))
            return false;
        if (std::filesystem::file_size(dmpPath) < 4096)
            return false;

        std::ifstream ifs(dmpPath, std::ios::binary);
        char magic[4]{};
        ifs.read(magic, 4);
        return ifs.good() &&
               magic[0] == 'M' &&
               magic[1] == 'D' &&
               magic[2] == 'M' &&
               magic[3] == 'P';
    }

    // Verify whether the current binary has an exact matching PDB (e.g. false in clean Release).
    bool TargetHasMatchingPdb()
    {
        HANDLE proc = GetCurrentProcess();
        SymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS);
        if (!SymInitializeW(proc, nullptr, TRUE))
            return false;

        IMAGEHLP_MODULE64 mod{};
        mod.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        BOOL ok = SymGetModuleInfo64(proc, reinterpret_cast<DWORD64>(&TargetHasMatchingPdb), &mod);
        SymCleanup(proc);
        return ok != FALSE && mod.SymType == SymPdb;
    }

    __forceinline void InlineWriteNull(volatile int* ptr)
    {
        *ptr = 42;
    }

    __declspec(noinline) void TriggerSubprocessAccessViolation()
    {
        volatile int* badPtr = nullptr;
        InlineWriteNull(badPtr);
    }

    __declspec(noinline) void TriggerSubprocessFatal()
    {
        Zero::CrashReporter::ReportFatal("Intentional fatal report test");
    }

    __declspec(noinline) void TriggerSubprocessTerminate()
    {
        std::terminate();
    }

    bool RunSubprocessCrash(const wchar_t* mode, const std::filesystem::path& dumpDir, DWORD& outExitCode)
    {
        wchar_t selfExe[MAX_PATH]{};
        DWORD len = GetModuleFileNameW(nullptr, selfExe, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return false;

        SetEnvironmentVariableW(L"ZERO_TEST_CRASH_MODE", mode);
        SetEnvironmentVariableW(L"ZERO_TEST_CRASH_DIR", dumpDir.c_str());

        std::wstring cmdLine = std::wstring(L"\"") + selfExe +
            L"\" --test-case=CrashReporter_SubprocessTrigger";

        HANDLE hDevNull = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE hNullRead = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hDevNull != INVALID_HANDLE_VALUE)
            SetHandleInformation(hDevNull, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        if (hNullRead != INVALID_HANDLE_VALUE)
            SetHandleInformation(hNullRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        if (hDevNull != INVALID_HANDLE_VALUE && hNullRead != INVALID_HANDLE_VALUE)
        {
            si.dwFlags    = STARTF_USESTDHANDLES;
            si.hStdInput  = hNullRead;
            si.hStdOutput = hDevNull;
            si.hStdError  = hDevNull;
        }

        PROCESS_INFORMATION pi{};
        BOOL created = CreateProcessW(
            selfExe,
            cmdLine.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi);

        SetEnvironmentVariableW(L"ZERO_TEST_CRASH_MODE", nullptr);
        SetEnvironmentVariableW(L"ZERO_TEST_CRASH_DIR", nullptr);

        if (hDevNull != INVALID_HANDLE_VALUE) CloseHandle(hDevNull);
        if (hNullRead != INVALID_HANDLE_VALUE) CloseHandle(hNullRead);

        if (!created)
            return false;

        DWORD waitRes = WaitForSingleObject(pi.hProcess, 20000);
        if (waitRes != WAIT_OBJECT_0)
        {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }

        GetExitCodeProcess(pi.hProcess, &outExitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
}

TEST_CASE("CrashReporter_SubprocessTrigger")
{
    wchar_t mode[64]{};
    DWORD modeLen = GetEnvironmentVariableW(L"ZERO_TEST_CRASH_MODE", mode, 64);
    if (modeLen == 0 || modeLen >= 64)
        return;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    wchar_t dumpDirW[MAX_PATH]{};
    DWORD dirLen = GetEnvironmentVariableW(L"ZERO_TEST_CRASH_DIR", dumpDirW, MAX_PATH);
    std::string dumpDirUtf8 = (dirLen > 0 && dirLen < MAX_PATH)
        ? PathToUtf8(std::filesystem::path(dumpDirW))
        : std::string();

    Zero::CrashReporterConfig cfg{};
    cfg.appName = "ZeroCrashTest";
    cfg.dumpDir = dumpDirUtf8.empty() ? nullptr : dumpDirUtf8.c_str();
    Zero::CrashReporter::Init(cfg);

    std::atomic<bool> stopWorker{ false };
    Zero::Thread worker([&stopWorker]()
    {
        while (!stopWorker.load(std::memory_order_relaxed))
            Sleep(10);
    });
    worker.SetName("CrashTestWorker");
    Sleep(30);

    std::wstring m(mode);
    if (m == L"av")
        TriggerSubprocessAccessViolation();
    else if (m == L"fatal")
        TriggerSubprocessFatal();
    else if (m == L"terminate")
        TriggerSubprocessTerminate();

    stopWorker.store(true, std::memory_order_relaxed);
    worker.Join();
    Zero::CrashReporter::Shutdown();
}

TEST_CASE("CrashReporter clean lifecycle")
{
    TempDirGuard guard{ MakeUniqueTempDir(L"Clean_caf\u00E9") };
    std::string dumpDirUtf8 = PathToUtf8(guard.path);

    Zero::CrashReporterConfig cfg{};
    cfg.appName = "ZeroCleanTest";
    cfg.dumpDir = dumpDirUtf8.c_str();

    Zero::CrashReporter::Init(cfg);
    Zero::CrashReporter::SetAppName("ZeroCleanRenamed");
    Zero::CrashReporter::Shutdown();

    CrashArtifacts artifacts = CollectArtifacts(guard.path);
    CHECK(artifacts.dmpPath.empty());
    CHECK(artifacts.logPath.empty());
}

TEST_CASE("CrashReporter out-of-process access violation capture")
{
    TempDirGuard guard{ MakeUniqueTempDir(L"AV_caf\u00E9") };

    DWORD exitCode = 0;
    REQUIRE(RunSubprocessCrash(L"av", guard.path, exitCode));
    // doctest's FatalConditionHandler (prevFilter) catches unhandled SEH and exits with 1.
    CHECK((exitCode == EXCEPTION_ACCESS_VIOLATION || exitCode == 1u));

    CrashArtifacts artifacts = CollectArtifacts(guard.path);
    REQUIRE(VerifyMinidumpHeader(artifacts.dmpPath));
    REQUIRE_FALSE(artifacts.logText.empty());

    CHECK(artifacts.logText.find("0xC0000005") != std::string::npos);
    CHECK(artifacts.logText.find("EXCEPTION_ACCESS_VIOLATION") != std::string::npos);
    CHECK(artifacts.logText.find("WRITE at address 0x0000000000000000") != std::string::npos);
    CHECK(artifacts.logText.find("ZeroCrashTests+0x") != std::string::npos);
    CHECK(artifacts.logText.find("[Registers]") != std::string::npos);
    CHECK(artifacts.logText.find("CrashTestWorker") != std::string::npos);

    if (TargetHasMatchingPdb())
    {
        CHECK(artifacts.logText.find("TestCrashReporter.cpp") != std::string::npos);
#ifdef NDEBUG
        CHECK(artifacts.logText.find("[inline]") != std::string::npos);
#endif
    }
}

TEST_CASE("CrashReporter out-of-process ReportFatal capture")
{
    TempDirGuard guard{ MakeUniqueTempDir(L"Fatal") };

    DWORD exitCode = 0;
    REQUIRE(RunSubprocessCrash(L"fatal", guard.path, exitCode));
    CHECK(exitCode == 0xE05A4552u);

    CrashArtifacts artifacts = CollectArtifacts(guard.path);
    REQUIRE(VerifyMinidumpHeader(artifacts.dmpPath));
    REQUIRE_FALSE(artifacts.logText.empty());

    CHECK(artifacts.logText.find("ZERO_FATAL_ABORT") != std::string::npos);
    CHECK(artifacts.logText.find("Intentional fatal report test") != std::string::npos);
    CHECK(artifacts.logText.find("ZeroCrashTests+0x") != std::string::npos);
    CHECK(artifacts.logText.find("CrashTestWorker") != std::string::npos);

    if (TargetHasMatchingPdb())
        CHECK(artifacts.logText.find("TestCrashReporter.cpp") != std::string::npos);
}

TEST_CASE("CrashReporter out-of-process std::terminate capture")
{
    TempDirGuard guard{ MakeUniqueTempDir(L"Term") };

    DWORD exitCode = 0;
    REQUIRE(RunSubprocessCrash(L"terminate", guard.path, exitCode));
    CHECK(exitCode == 0xE05A4552u);

    CrashArtifacts artifacts = CollectArtifacts(guard.path);
    REQUIRE(VerifyMinidumpHeader(artifacts.dmpPath));
    REQUIRE_FALSE(artifacts.logText.empty());

    CHECK(artifacts.logText.find("ZERO_FATAL_ABORT") != std::string::npos);
    CHECK(artifacts.logText.find("std::terminate() called") != std::string::npos);
    CHECK(artifacts.logText.find("ZeroCrashTests+0x") != std::string::npos);

    if (TargetHasMatchingPdb())
        CHECK(artifacts.logText.find("TestCrashReporter.cpp") != std::string::npos);
}

#endif

int main(int argc, char** argv)
{
    Zero::Log::SetEnabled(false);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}

