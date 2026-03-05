#pragma once
#include "Core.h"

namespace Zero
{
    class ENGINE_API Time
    {
    public:
        static void Init();
        static void Update();

        static float GetTime();      // Seconds since engine start
        static float GetDeltaTime(); // Seconds between frames

    private:
        static double s_StartTime;
        static double s_CurrentTime;
        static double s_DeltaTime;
    };
}
