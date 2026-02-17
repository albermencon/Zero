#include "pch.h"
#include <Engine/Time.h>
#include <chrono>

namespace VoxelEngine
{
    using Clock = std::chrono::steady_clock;

    static Clock::time_point s_EngineStart;
    static Clock::time_point s_LastFrame;

    double Time::s_StartTime = 0.0;
    double Time::s_CurrentTime = 0.0;
    double Time::s_DeltaTime = 0.0;

    bool initialized = false;
    void Time::Init()
    {
        if (initialized)
            return;
        s_EngineStart = Clock::now();
        s_LastFrame = s_EngineStart;
        initialized = true;
    }

    void Time::Update()
    {
        auto now = Clock::now();

        s_CurrentTime = std::chrono::duration<double>(now - s_EngineStart).count();
        s_DeltaTime = std::chrono::duration<double>(now - s_LastFrame).count();

        s_LastFrame = now;
    }

    float Time::GetTime()
    {
        return static_cast<float>(s_CurrentTime);
    }

    float Time::GetDeltaTime()
    {
        return static_cast<float>(s_DeltaTime);
    }
}
