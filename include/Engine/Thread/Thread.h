#pragma once
#include <Engine/Core.h>
#include "ThreadPriority.h"
#include <cstdint>
#include <string_view>
#include <string>
#include <utility>
#include <type_traits>
#include <functional>

#ifdef Yield
#undef Yield
#endif

namespace Zero
{
    using ThreadId = uint64_t;

    class Thread
    {
    public:
        Thread() = default;

        template<typename F, typename... Args>
        requires (!std::is_integral_v<std::decay_t<F>>)
        explicit Thread(F&& func, Args&&... args)
            : Thread(static_cast<size_t>(0), std::forward<F>(func), std::forward<Args>(args)...)
        {
        }

        template<typename F, typename... Args>
        explicit Thread(size_t stackSize, F&& func, Args&&... args)
        {
            auto task = [f = std::forward<F>(func), ...args = std::forward<Args>(args)]() mutable {
                std::invoke(f, std::forward<Args>(args)...);
            };
            using TaskType = decltype(task);
            TaskType* targetTask = new TaskType(std::move(task));

            Start(stackSize, [](void* param) -> void* {
                TaskType* t = static_cast<TaskType*>(param);
                (*t)();
                delete t;
                return nullptr;
            }, targetTask);
        }



        ~Thread();

        Thread(Thread&& other) noexcept
            : m_name(std::move(other.m_name))
            , m_handle(other.m_handle)
            , m_id(other.m_id)
        {
            other.m_handle = nullptr;
            other.m_id = 0;
        }

        Thread& operator=(Thread&& other) noexcept
        {
            if (this != &other)
            {
                Detach();
                m_name = std::move(other.m_name);
                m_handle = other.m_handle;
                m_id = other.m_id;
                other.m_handle = nullptr;
                other.m_id = 0;
            }
            return *this;
        }

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        void Join();
        bool Joinable() const;
        ThreadId GetId() const;

        static ThreadId GetCurrentThreadId();

        void SetName(std::string_view name);
        void SetPriority(ThreadPriority priority);
        void SetAffinity(uint64_t mask);

        static void Sleep(uint32_t ms);
        static void Yield();

    private:
        void Start(size_t stackSize, void*(*threadFunc)(void*), void* param);
        void Detach();
        void* NativeHandle();

        std::string   m_name;
        void*         m_handle = nullptr;
        uint64_t      m_id = 0;
    };

    namespace this_thread
    {
        static void SetName(std::string_view name);
        static void SetPriority(ThreadPriority priority);
        static void SetAffinity(uint64_t mask);
    }
}
