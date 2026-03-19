#include "pch.h"
#include <Engine/Core.h>
#ifdef PLATFORM_WINDOWS

#include "../PlatformThread.h"
#include <windows.h>
#include <string>

// SetThreadDescription requires Windows 10 1607+ — load dynamically for compat
using FnSetThreadDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);

static FnSetThreadDescription LoadSetThreadDescription()
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32)
        return reinterpret_cast<FnSetThreadDescription>(
            GetProcAddress(kernel32, "SetThreadDescription"));
    return nullptr;
}

static FnSetThreadDescription s_SetThreadDescription = LoadSetThreadDescription();

#pragma warning(push)
#pragma warning(disable: 4535) // RaiseException used without /EHa

// Separate function with no C++ objects so __try is legal
static void SetNameLegacySEH(DWORD threadId, const char* name)
{
    constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push, 8)
    struct THREADNAME_INFO
    {
        DWORD  dwType;      // Must be 0x1000
        LPCSTR szName;
        DWORD  dwThreadID;
        DWORD  dwFlags;
    };
#pragma pack(pop)

    THREADNAME_INFO info{ 0x1000, name, threadId, 0 };

    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0,
            sizeof(info) / sizeof(ULONG_PTR),
            reinterpret_cast<const ULONG_PTR*>(&info));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

#pragma warning(pop)

namespace Zero::PlatformThread
{
    void SetName(void* nativeHandle, std::string_view name)
    {
        HANDLE handle = static_cast<HANDLE>(nativeHandle);

        if (s_SetThreadDescription)
        {
            // Convert UTF-8 to UTF-16
            int wlen = MultiByteToWideChar(CP_UTF8, 0,
                name.data(), static_cast<int>(name.size()), nullptr, 0);

            std::wstring wname(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0,
                name.data(), static_cast<int>(name.size()),
                wname.data(), wlen);

            s_SetThreadDescription(handle, wname.c_str());
            return;
        }

        // Legacy path: no C++ objects past this point before the SEH call,
        // so we delegate to the plain-C helper above
        std::string nameStr(name); // build std::string BEFORE entering SEH scope
        SetNameLegacySEH(GetThreadId(handle), nameStr.c_str());
    }

    void SetPriority(void* nativeHandle, ThreadPriority priority)
    {
        HANDLE handle = static_cast<HANDLE>(nativeHandle);
        int winPriority;
        switch (priority)
        {
        case ThreadPriority::Low:      winPriority = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::Normal:   winPriority = THREAD_PRIORITY_NORMAL;       break;
        case ThreadPriority::High:     winPriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::Critical: winPriority = THREAD_PRIORITY_TIME_CRITICAL; break;
        default:                       winPriority = THREAD_PRIORITY_NORMAL;       break;
        }
        SetThreadPriority(handle, winPriority);
    }

    void SetAffinity(void* nativeHandle, uint64_t mask)
    {
        SetThreadAffinityMask(static_cast<HANDLE>(nativeHandle),
            static_cast<DWORD_PTR>(mask));
    }

    void Sleep(uint32_t ms)
    {
        ::Sleep(static_cast<DWORD>(ms));
    }

    void YieldT()
    {
        SwitchToThread(); // more cooperative than Sleep(0) on Windows
    }
}
#endif
