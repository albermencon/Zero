#pragma once
#include <Engine/Core.h>
#include "ThreadPriority.h"
#include <cstdint>
#include <thread>
#include <string_view>
#include <string>

namespace Zero
{
    using ThreadId = std::thread::id;

    class Thread
    {
    public:
        Thread() = default;

        template<typename F, typename... Args>
        explicit Thread(F&& func, Args&&... args)
            : m_thread(std::forward<F>(func), std::forward<Args>(args)...)
        {
        }

        ~Thread() { if (Joinable()) m_thread.detach(); }

        Thread(Thread&&) noexcept = default;
        Thread& operator=(Thread&&) noexcept = default;
        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        void Join();
        bool Joinable() const;
        ThreadId GetId() const;

        static ThreadId GetCurrentThreadId();

        // Platform-specific — implemented per-platform
        void SetName(std::string_view name);
        void SetPriority(ThreadPriority priority);
        void SetAffinity(uint64_t mask);

        static void Sleep(uint32_t ms);
        static void Yield();

    private:
        // Returns the native handle as void* for platform layer
        void* NativeHandle();

        std::string   m_name;
        std::thread   m_thread;
    };
}