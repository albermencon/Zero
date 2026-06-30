#include "pch.h"
#include <Engine/Core.h>
#ifdef PLATFORM_WINDOWS

#include <Engine/Thread/Thread.h>
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/RecursiveMutex.h>
#include <Engine/Thread/ConditionVariable.h>
#include <Engine/Thread/Semaphore.h>
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

namespace Zero
{
    Thread::~Thread()
    {
        Detach();
    }

    void Thread::Start(size_t stackSize, void*(*threadFunc)(void*), void* param)
    {
        DWORD flags = 0;
        if (stackSize > 0)
        {
            flags = STACK_SIZE_PARAM_IS_A_RESERVATION;
        }

        DWORD threadId = 0;
        m_handle = CreateThread(
            nullptr,
            static_cast<SIZE_T>(stackSize),
            reinterpret_cast<LPTHREAD_START_ROUTINE>(threadFunc),
            param,
            flags,
            &threadId
        );
        m_id = static_cast<uint64_t>(threadId);
    }

    void Thread::Detach()
    {
        if (Joinable())
        {
            CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
            m_id = 0;
        }
    }

    void Thread::Join()
    {
        if (Joinable())
        {
            WaitForSingleObject(static_cast<HANDLE>(m_handle), INFINITE);
            CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
            m_id = 0;
        }
    }

    bool Thread::Joinable() const
    {
        return m_handle != nullptr;
    }

    ThreadId Thread::GetId() const
    {
        return m_id;
    }

    ThreadId Thread::GetCurrentThreadId()
    {
        return static_cast<uint64_t>(::GetCurrentThreadId());
    }

    static_assert(sizeof(SRWLOCK) <= 64, "SRWLOCK size exceeds storage buffer");
    static_assert(alignof(SRWLOCK) <= 8, "SRWLOCK alignment exceeds storage buffer");

    Mutex::Mutex()
    {
        InitializeSRWLock(reinterpret_cast<PSRWLOCK>(m_storage));
    }
    
    Mutex::~Mutex() {}
    void Mutex::Lock() { AcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(m_storage)); }
    void Mutex::Unlock() { ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(m_storage)); }
    bool Mutex::TryLock() { return TryAcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(m_storage)) != 0; }

    static_assert(sizeof(CRITICAL_SECTION) <= 64, "CRITICAL_SECTION size exceeds storage buffer");
    static_assert(alignof(CRITICAL_SECTION) <= 8, "CRITICAL_SECTION alignment exceeds storage buffer");

    RecursiveMutex::RecursiveMutex()
    {
        InitializeCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(m_storage));
    }

    RecursiveMutex::~RecursiveMutex()
    {
        DeleteCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(m_storage));
    }

    void RecursiveMutex::Lock() { EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(m_storage)); }
    void RecursiveMutex::Unlock() { LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(m_storage)); }
    bool RecursiveMutex::TryLock() { return TryEnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(m_storage)) != 0; }

    static_assert(sizeof(CONDITION_VARIABLE) <= 48, "CONDITION_VARIABLE size exceeds storage buffer");
    static_assert(alignof(CONDITION_VARIABLE) <= 8, "CONDITION_VARIABLE alignment exceeds storage buffer");

    ConditionVariable::ConditionVariable()
    {
        InitializeConditionVariable(reinterpret_cast<PCONDITION_VARIABLE>(m_storage));
    }

    ConditionVariable::~ConditionVariable() {}
    void ConditionVariable::Wait(Mutex& mutex)
    {
        SleepConditionVariableSRW(reinterpret_cast<PCONDITION_VARIABLE>(m_storage), reinterpret_cast<PSRWLOCK>(mutex.GetNativeHandle()), INFINITE, 0);
    }

    void ConditionVariable::NotifyOne()
    {
        WakeConditionVariable(reinterpret_cast<PCONDITION_VARIABLE>(m_storage));
    }

    void ConditionVariable::NotifyAll()
    {
        WakeAllConditionVariable(reinterpret_cast<PCONDITION_VARIABLE>(m_storage));
    }

    static_assert(sizeof(HANDLE) <= 32, "HANDLE size exceeds storage buffer");
    static_assert(alignof(HANDLE) <= 8, "HANDLE alignment exceeds storage buffer");

    Semaphore::Semaphore(int initialCount)
    {
        HANDLE handle = CreateSemaphoreA(nullptr, initialCount, LONG_MAX, nullptr);
        std::memcpy(m_storage, &handle, sizeof(HANDLE));
    }

    Semaphore::~Semaphore()
    {
        HANDLE handle;
        std::memcpy(&handle, m_storage, sizeof(HANDLE));
        CloseHandle(handle);
    }

    void Semaphore::Wait()
    {
        HANDLE handle;
        std::memcpy(&handle, m_storage, sizeof(HANDLE));
        WaitForSingleObject(handle, INFINITE);
    }

    void Semaphore::Signal()
    {
        HANDLE handle;
        std::memcpy(&handle, m_storage, sizeof(HANDLE));
        ReleaseSemaphore(handle, 1, nullptr);
    }
}
#endif
