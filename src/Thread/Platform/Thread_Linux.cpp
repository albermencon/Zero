#include "pch.h"
#include <Engine/Core.h>
#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)

#include "../PlatformThread.h"
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <cstring>
#include <algorithm>
#include <string>

namespace Zero::PlatformThread
{
    void SetName(void* nativeHandle, std::string_view name)
    {
        pthread_t handle = reinterpret_cast<pthread_t>(nativeHandle);

        // pthread limit: 15 chars + null
        char buf[16];
        size_t len = std::min(name.size(), static_cast<size_t>(15));
        std::memcpy(buf, name.data(), len);
        buf[len] = '\0';

        pthread_setname_np(handle, buf);
    }

    void SetPriority(void* nativeHandle, ThreadPriority priority)
    {
        pthread_t handle = reinterpret_cast<pthread_t>(nativeHandle);

        // SCHED_OTHER (default CFS) ignores sched_priority — use nice-like approach.
        // For real-time: SCHED_RR with priority 1-99.
        // Critical → SCHED_RR; High → SCHED_RR low; others → SCHED_OTHER + nice
        sched_param param{};
        int policy;

        switch (priority)
        {
        case ThreadPriority::Critical:
            policy = SCHED_RR;
            param.sched_priority = sched_get_priority_max(SCHED_RR);
            break;
        case ThreadPriority::High:
            policy = SCHED_RR;
            param.sched_priority = sched_get_priority_min(SCHED_RR) + 1;
            break;
        case ThreadPriority::Normal:
        case ThreadPriority::Low:
        default:
            policy = SCHED_OTHER;
            param.sched_priority = 0; // must be 0 for SCHED_OTHER
            break;
        }

        // Requires CAP_SYS_NICE or appropriate rlimit for RT policies
        pthread_setschedparam(handle, policy, &param);
    }

    void SetAffinity(void* nativeHandle, uint64_t mask)
    {
        pthread_t handle = reinterpret_cast<pthread_t>(nativeHandle);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < 64; ++i)
        {
            if (mask & (UINT64_C(1) << i))
                CPU_SET(i, &cpuset);
        }
        pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
    }

    void Sleep(uint32_t ms)
    {
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = static_cast<long>(ms % 1000) * 1'000'000L;
        // Handle EINTR — restart if interrupted by signal
        while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
    }

    void YieldT()
    {
        sched_yield();
    }
}
#endif
