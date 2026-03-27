#pragma once
#include <Engine/Core.h>
#include <cstdint>
#include <cstring>
#include "JobCounter.h"

namespace Zero
{
    struct ENGINE_API Job
    {
        using JobFn = void(*)(void*);

        JobFn fn;
        JobCounter* counter{ nullptr }; // null = fire-and-forget

        union
        {
            void* ptr;
            uint8_t payload[40];
        };

        enum class Mode : uint8_t
        {
            Inline,
            External
        };

        Mode mode;
    };

    static_assert(sizeof(Job) == 64);
}
